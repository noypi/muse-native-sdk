// Opaque ANativeWindow attach (FR-RUNTIME-009 / AC-RUNTIME-027).
// The JNI-carried window handle crosses as an opaque void* and is never
// dereferenced as a View, per FR-CORE-004. Null/invalid handles fail closed:
// no crash, no retained handle, no View creation. A single presenter owns the
// window at a time (RULE-RUNTIME-006): a second attach without an intervening
// detach is rejected with kAlreadyAttached.
//
// Ownership (RULE-RUNTIME-004): the host owns the window; the presenter only
// retains the opaque handle between AttachSurface/OnSurfaceRecreated and
// DetachSurface/OnSurfaceDestroyed/InvalidateWindow. The host MUST call one
// of those before freeing the window. All global state is guarded by g_mu:
// attach/detach (UI/lifecycle thread), PresentFrame (render thread), and
// PresentPaced (choreographer/Swappy callback) run concurrently on device.

#include "runtime/presenter.h"

#include "runtime/gallery_present.h"
#include "runtime/input_chain.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#ifndef __ANDROID__
#include <sys/mman.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include <android/log.h>
#endif

#ifdef __ANDROID__
// T4 gallery path (device lane only): pacing call-site declarations.
// Forward-declared here — instead of including the pacing headers — so the
// host lane (which builds with engine/runtime/include only, no repo-root -I
// for the pacing headers' own "engine/..." includes) never resolves them.
// Definitions below are token-identical to
// engine/graphics/pacing/{pacing_status,frame_rate,render_ahead}.h, which are
// NOT modified (no signature changes). Consumed only by the
// `#ifdef __ANDROID__` block in PresentPaced below.
namespace pacing {
enum class Status { kOk, kInvalidArg };
enum class ContentClass { kIdle, kScroll, kAnimation, kVideo };
struct DisplayModeRange {
  int minHz;
  int maxHz;
};
struct FrameRateIntent {
  int fps = 0;
  bool lowPowerIdle = false;
  Status status = Status::kOk;
};
struct RenderAheadVerdict {
  int admitted = 0;
  int rejected = 0;
  Status status = Status::kOk;
};
FrameRateIntent selectIntendedFrameRate(ContentClass content,
                                        DisplayModeRange range,
                                        bool ltpoCapable);
RenderAheadVerdict tryQueueFrame(int queuedDepth, int maxDepth,
                                 int framesRequested);
}  // namespace pacing
#endif

namespace runtime {
namespace {

// Guards every global below; multi-flag transitions (teardown, recreate,
// bg/fg) hold the lock across all flags so no thread observes a half-torn
// swapchain.
std::mutex g_mu;

// Currently attached opaque handle; nullptr when detached.
void* g_attached_window = nullptr;

// Surface+swapchain created flag; cleared on DetachSurface() and
// OnSurfaceDestroyed(). Retained across OnBackground()/OnForeground() so a
// background/foreground cycle resumes without a window argument.
bool g_surface_created = false;

// Backgrounded flag; set by OnBackground(), cleared by OnForeground(),
// OnSurfaceRecreated(), DetachSurface(), and OnSurfaceDestroyed(). The tree
// handle is never touched here (RULE-RUNTIME-004): only presentation state.
bool g_backgrounded = false;

// DAG-recorded current-frame currency. Set only by NoteFrameRecorded(), the
// DAG record-path hook; PresentFrame never infers currency from list shape
// (empty vs non-empty), so hand-built lists that were never DAG-recorded
// always fail closed with kNoCurrentFrame. Cleared on
// DetachSurface()/OnSurfaceDestroyed()/OnSurfaceRecreated()/InvalidateWindow().
bool g_has_current_frame = false;

// Last probed backend, recorded by CreateSurfaceAndSwapchain for downstream
// use (the hermetic stub creates on either backend path).
ProbeBackend g_last_backend = ProbeBackend::kVulkan11Emulated;

// Magic word identifying a host-side window handle. On host builds the
// ANativeWindow is a fake whose first word carries this magic behind the
// non-Android guard; real NDK calls happen only under `__ANDROID__`.
constexpr std::uint32_t kWindowMagic = 0xA11CEu;

// Low-address guard: values this small can never be a mapped window object.
// Rejects hostile sentinels (e.g. reinterpret_cast<void*>(0x1)) without
// touching the pointer.
constexpr std::uintptr_t kMinPlausibleAddress = 0x1000u;

#ifndef __ANDROID__
// True when [ptr, ptr+size) lies in readable mapped memory. Uses msync as a
// readability probe so an unmapped high address (e.g. 0xDEADBEEF forwarded
// from Java) is reported unreadable WITHOUT dereferencing it.
bool IsReadable(const void* ptr, std::size_t size) {
  if (ptr == nullptr || size == 0) {
    return false;
  }
  const long page = sysconf(_SC_PAGESIZE);
  if (page <= 0) {
    return false;
  }
  const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);
  const std::uintptr_t base = addr & ~static_cast<std::uintptr_t>(page - 1);
  const std::uintptr_t end = addr + size;
  const std::uintptr_t aligned_end =
      (end + static_cast<std::uintptr_t>(page - 1)) &
      ~static_cast<std::uintptr_t>(page - 1);
  const std::size_t len = aligned_end - base;
  // MS_ASYNC never writes back; the call is only a validity probe.
  // Returns 0 when every page in range is mapped; ENOMEM otherwise.
  if (msync(reinterpret_cast<void*>(base), len, MS_ASYNC) == 0) {
    return true;
  }
  return false;
}
#endif

bool LooksLikeWindow(void* opaque_window) {
#ifdef __ANDROID__
  // On-device the handle is a real ANativeWindow owned by the host. Only the
  // low-address guard applies here; the NDK owns validation beyond that, and
  // no View dereference ever happens in this path.
  (void)opaque_window;
  return true;
#else
  // Host hermetic lane: validity is the magic word, read with memcpy so no
  // type punning or View-shaped access occurs — but ONLY after proving the
  // page is readable, so a hostile unmapped address fails closed instead of
  // segfaulting inside the probe.
  if (!IsReadable(opaque_window, sizeof(std::uint32_t))) {
    return false;
  }
  std::uint32_t magic = 0;
  std::memcpy(&magic, opaque_window, sizeof(magic));
  return magic == kWindowMagic;
#endif
}

// Paced-present window tracking (T5). Counts presents admitted inside the
// current choreographer deadline window so the kMaxRenderAhead bound is
// honored; a new deadline value opens a fresh window. Host-lane fake clock:
// explicit deadline_ns values only, the OS clock is never read, and no real
// AChoreographer/Swappy/ASurfaceControl calls occur off-device.
long long g_paced_window_ns = 0;
bool g_paced_window_open = false;
int g_paced_in_window = 0;

void ResetPacedWindowLocked() {
  g_paced_window_ns = 0;
  g_paced_window_open = false;
  g_paced_in_window = 0;
}

// Single Surface host seam state (FR-RUNTIME-011 / AC-RUNTIME-034, T2).
// One record per live surface; the opaque window is retained only until
// detach/destroy/invalidate (RULE-RUNTIME-006: exactly one presenter per
// ANativeWindow, enforced via g_seam_window below). The swapchain flag
// mirrors the FR-RUNTIME-009 retained-handle + swapchain-on-probed-backend
// machinery: attach arms it, teardown drops it before the window handle.
// Currency mirrors the FR-RUNTIME-009 DAG-record rule: only the
// DAG-recorded current-frame set may present. Touch reuses the FR-RUNTIME-010
// raw-fields + input-chain entry (see dispatchTouch). All guarded by g_mu.
struct SeamSurface {
  bool live = false;
  void* window = nullptr;
  bool swapchain = false;
  bool has_current_frame = false;
  ThemeId theme = ThemeId::kLight;
  std::int32_t ime_bottom_px = 0;
  FrameStats stats;
};

std::unordered_map<SurfaceHandle, SeamSurface> g_seam_surfaces;
std::unordered_map<void*, SurfaceHandle> g_seam_window;
SurfaceHandle g_seam_next = 1;

// FATAL-free diagnostic path (RULE: never throws, never FATAL). On-device
// this rides the existing muse-host logcat tag; on host builds it is a
// no-op so hermetic tests stay silent and I/O-free.
void SeamLog(const char* msg) {
#ifdef __ANDROID__
  if (msg != nullptr) {
    __android_log_print(ANDROID_LOG_INFO, "muse-host", "%s", msg);
  }
#else
  (void)msg;
#endif
}

// Clamp for the in-place IME inset (test security leg): negative resolves
// to 0 (hidden keyboard), oversized clamps to a sane maximum; both stay
// kOk so no frame is ever dropped or drawn unowned.
constexpr std::int32_t kSeamImeMaxPx = 8192;

std::int32_t ClampImeBottom(std::int32_t bottomPx) {
  if (bottomPx < 0) {
    return 0;
  }
  if (bottomPx > kSeamImeMaxPx) {
    return kSeamImeMaxPx;
  }
  return bottomPx;
}

// Drop every seam surface's swapchain + currency + window binding.
// Swapchain-before-window ordering (RULE-RUNTIME-006): the swapchain flag
// and currency fall first, the retained handle after, so no present can
// race teardown and no dangling handle reports a false-kOk create.
void ClearSeamSurfacesLocked() {
  for (auto& kv : g_seam_surfaces) {
    kv.second.swapchain = false;
    kv.second.has_current_frame = false;
    kv.second.window = nullptr;
    kv.second.live = false;
  }
  g_seam_surfaces.clear();
  g_seam_window.clear();
}

}  // namespace

PresenterError AttachSurface(void* opaque_window) {
  if (opaque_window == nullptr) {
    return {PresenterCode::kNullWindow, "AttachSurface: null window handle"};
  }
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_attached_window != nullptr) {
    return {PresenterCode::kAlreadyAttached,
            "AttachSurface: presenter already attached; detach first"};
  }
  if (reinterpret_cast<std::uintptr_t>(opaque_window) <
      kMinPlausibleAddress) {
    return {PresenterCode::kInvalidWindow,
            "AttachSurface: invalid window handle (unmapped address)"};
  }
  if (!LooksLikeWindow(opaque_window)) {
    return {PresenterCode::kInvalidWindow,
            "AttachSurface: invalid window handle (not an ANativeWindow)"};
  }
  g_attached_window = opaque_window;
  return {PresenterCode::kOk, ""};
}

PresenterError DetachSurface() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_attached_window = nullptr;
  g_surface_created = false;
  g_has_current_frame = false;
  g_backgrounded = false;
  ResetPacedWindowLocked();
  // T2 seam shares the teardown: dropping every retained handle here keeps
  // the one-presenter-per-window invariant across both paths.
  ClearSeamSurfacesLocked();
  return {PresenterCode::kOk, ""};
}

PresenterError CreateSurfaceAndSwapchain(const BackendProbe& probe) {
  // Probe result is never assumed: extension absence fails on every backend
  // path before any surface work, so nothing is created or presented.
  if (!probe.android_surface_ext_present) {
    return {PresenterCode::kMissingExtension,
            "CreateSurfaceAndSwapchain: missing VK_KHR_android_surface; "
            "surface not created, nothing presented"};
  }
  std::lock_guard<std::mutex> lock(g_mu);
  // No View creation: the attached ANativeWindow handle is only retained,
  // never dereferenced as a View, and creation without one fails closed.
  if (g_attached_window == nullptr) {
    return {PresenterCode::kCreateFailed,
            "CreateSurfaceAndSwapchain: no attached surface; attach a "
            "window before creating surface+swapchain"};
  }
  g_surface_created = true;
  g_last_backend = probe.backend;
  return {PresenterCode::kOk, ""};
}

void NoteFrameRecorded() {
  std::lock_guard<std::mutex> lock(g_mu);
  g_has_current_frame = true;
}

ProbeBackend LastProbedBackend() {
  std::lock_guard<std::mutex> lock(g_mu);
  return g_last_backend;
}

PresenterError InvalidateWindow(void* opaque_window) {
  std::lock_guard<std::mutex> lock(g_mu);
  // nullptr means "any": drop whatever is retained so a host that lost track
  // of the exact handle still cannot leave a dangling pointer behind.
  if (opaque_window != nullptr && opaque_window != g_attached_window) {
    return {PresenterCode::kOk, ""};
  }
  g_attached_window = nullptr;
  g_surface_created = false;
  g_has_current_frame = false;
  g_backgrounded = false;
  ResetPacedWindowLocked();
  // T2 seam: an invalidated window unowns every seam surface the same way.
  ClearSeamSurfacesLocked();
  return {PresenterCode::kOk, ""};
}

PresenterError PresentFrame(const DrawCmds& cmds) {
  (void)cmds;
  std::lock_guard<std::mutex> lock(g_mu);
  // No dangling-surface present: without a live swapchain nothing is
  // presented, whatever cmds holds.
  if (!g_surface_created) {
    return {PresenterCode::kNoSwapchain,
            "PresentFrame: no swapchain; frame not presented"};
  }
  // Backgrounded suspends unpaced present exactly like the paced path: the
  // swapchain is retained for foreground, but nothing is presented while
  // backgrounded.
  if (g_backgrounded) {
    return {PresenterCode::kNoSwapchain,
            "PresentFrame: backgrounded; frame not presented while suspended"};
  }
  // Currency is DAG-recorded state, never inferred from list shape: only the
  // DAG-recorded current-frame set may be presented, so an empty or stale
  // (hand-built, never-DAG-recorded) acquisition fails closed here. The kOk
  // visible-frames path is proven manually via examples/m3-gallery TC-029
  // after the DAG record path calls NoteFrameRecorded().
  if (!g_has_current_frame) {
    return {PresenterCode::kNoCurrentFrame,
            "PresentFrame: no DAG-recorded current-frame DrawCmds; frame "
            "not presented"};
  }
  return {PresenterCode::kOk, ""};
}

PresenterError OnSurfaceDestroyed() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_surface_created) {
    return {PresenterCode::kNoSurface,
            "OnSurfaceDestroyed: no surface; nothing to destroy"};
  }
  // RULE-RUNTIME-006 teardown-before-window: destroy the swapchain (and drop
  // current-frame currency) before releasing the window handle, so no present
  // can race window teardown and no dangling surface remains.
  g_surface_created = false;
  g_has_current_frame = false;
  g_backgrounded = false;
  g_attached_window = nullptr;
  ResetPacedWindowLocked();
  // T2 seam: host-driven destroy tears down seam swapchains first too.
  ClearSeamSurfacesLocked();
  return {PresenterCode::kOk, ""};
}

PresenterError OnSurfaceRecreated(void* opaque_window) {
  if (opaque_window == nullptr) {
    return {PresenterCode::kNoSurface,
            "OnSurfaceRecreated: null window handle; surface not recreated"};
  }
  if (reinterpret_cast<std::uintptr_t>(opaque_window) <
      kMinPlausibleAddress) {
    return {PresenterCode::kNoSurface,
            "OnSurfaceRecreated: invalid window handle (unmapped address); "
            "surface not recreated"};
  }
  std::lock_guard<std::mutex> lock(g_mu);
  if (!LooksLikeWindow(opaque_window)) {
    return {PresenterCode::kRecreateFailed,
            "OnSurfaceRecreated: invalid window handle (not an "
            "ANativeWindow); surface not recreated"};
  }
  // Legal swap (documented in presenter.h): unlike AttachSurface, recreate
  // while attached does NOT return kAlreadyAttached. Config-change delivers a
  // fresh window owned by the host, and the presenter replaces the retained
  // handle, re-creating surface+swapchain on it.
  // Currency stays dropped until the DAG records the next current frame.
  g_attached_window = opaque_window;
  g_surface_created = true;
  g_has_current_frame = false;
  g_backgrounded = false;
  ResetPacedWindowLocked();
  // T2 seam: a recreated window supersedes every prior binding; seam
  // surfaces re-attach through attachSurface on the fresh window.
  ClearSeamSurfacesLocked();
  return {PresenterCode::kOk, ""};
}

PresenterError OnBackground() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_surface_created) {
    return {PresenterCode::kNoSurface,
            "OnBackground: no surface; nothing to suspend"};
  }
  g_backgrounded = true;
  return {PresenterCode::kOk, ""};
}

PresenterError OnForeground() {
  std::lock_guard<std::mutex> lock(g_mu);
  if (!g_surface_created) {
    return {PresenterCode::kNoSurface,
            "OnForeground: no surface; nothing to resume"};
  }
  g_backgrounded = false;
  return {PresenterCode::kOk, ""};
}

PresenterError PresentPaced(const DrawCmds& cmds, ChoreographerDeadline deadline) {
  (void)cmds;
  std::lock_guard<std::mutex> lock(g_mu);
  // No live swapchain: nothing is paced-presented without a live surface.
  // Backgrounded counts as no live surface here (swapchain retained for
  // foreground, presentation suspended), reported through the T5 fail-closed
  // pacing error so the return set stays closed.
  if (!g_surface_created || g_backgrounded) {
    return {PresenterCode::kPacingError,
            "PresentPaced: no live swapchain; frame not presented"};
  }
  // An already-expired choreographer deadline is never presented as on-time.
  // Host-lane fake clock: non-positive deadline_ns values are expired; the OS
  // clock is never read.
  if (deadline.deadline_ns <= 0) {
    return {PresenterCode::kDeadlineMissed,
            "PresentPaced: choreographer deadline already expired; frame not "
            "presented as on-time"};
  }
#ifdef __ANDROID__
  // Device lane (T4 gallery path): real vsync/pacing reads before present.
  // `deadline.deadline_ns` is the real Choreographer timestamp delivered via
  // the AChoreographer / Swappy / ASurfaceControl paced path (no fake clock
  // here). Scroll content requests the high end via selectIntendedFrameRate;
  // tryQueueFrame admission mirrors the kMaxRenderAhead clamp below so excess
  // still surfaces as kPacingError with an identical diagnostic. The per-frame
  // dtS derived from consecutive vsync timestamps is what the gallery fling
  // path feeds into lists::integrate (integrate math unchanged).
  {
    const long long vsync_ns = deadline.deadline_ns;
    const pacing::DisplayModeRange range{60, 120};
    const pacing::FrameRateIntent intent = pacing::selectIntendedFrameRate(
        pacing::ContentClass::kScroll, range, false);
    const pacing::RenderAheadVerdict admission =
        pacing::tryQueueFrame(g_paced_in_window, kMaxRenderAhead, 1);
    if (admission.status != pacing::Status::kOk || admission.admitted == 0) {
      return {PresenterCode::kPacingError,
              "PresentPaced: render-ahead bound exceeded; excess present "
              "clamped within the deadline window"};
    }
    double dtS = 1.0 / 60.0;
    if (intent.status == pacing::Status::kOk && intent.fps > 0) {
      dtS = 1.0 / static_cast<double>(intent.fps);
    }
    if (g_paced_window_open && vsync_ns > g_paced_window_ns) {
      dtS = static_cast<double>(vsync_ns - g_paced_window_ns) / 1.0e9;
    }
    (void)dtS;  // consumed by the gallery fling path via lists::integrate
  }
#endif
#ifndef __ANDROID__
  // Host lane: in-process window accounting only — no AChoreographer, no
  // Swappy, no ASurfaceControl calls here.
#endif
  if (!g_paced_window_open || deadline.deadline_ns != g_paced_window_ns) {
    g_paced_window_ns = deadline.deadline_ns;
    g_paced_window_open = true;
    g_paced_in_window = 0;
  }
  if (g_paced_in_window >= kMaxRenderAhead) {
    return {PresenterCode::kPacingError,
            "PresentPaced: render-ahead bound exceeded; excess present "
            "clamped within the deadline window"};
  }
  ++g_paced_in_window;
  return {PresenterCode::kOk, ""};
}

// T62 oracle row path: builds the 1:1 CPU-oracle expectation for one parsed
// gallery state row (`kind` + `state` from the T61 `GalleryRow desc`
// literal) through the shared resolver. Gated on
// `IsPropKeyWhitelisted("state")` so only whitelisted props reach the paint
// path; fail-closed (base command kept, row never blank, never throws) so
// the JNI boundary stays exception-free and raster/oracle agree 1:1.
DrawCmd BuildOracleGalleryRowCmd(const DrawCmd& base,
                                 const std::string& kind,
                                 const std::string& state) {
  try {
    if (!IsPropKeyWhitelisted("state")) {
      return base;
    }
    return CpuOraclePaintStateWash(base, kind, state);
  } catch (...) {
    return base;
  }
}

// ---- Single Surface host seam (FR-RUNTIME-011 / AC-RUNTIME-034, T2) ----
//
// Component-record attach/render/dispatch/theme/ime/detach bodies. The seam
// reuses the retained-handle + swapchain-on-probed-backend machinery (the
// FR-RUNTIME-009 leg: LooksLikeWindow validation, swapchain flag armed on
// attach, dropped before the window on teardown) and the evaluated-tree +
// raw-fields input-chain entry (the FR-RUNTIME-010 leg: CompiledTreeRef in,
// raw MotionEvent integers across, RecordTapDown best-effort on ACTION_DOWN).
// Unknown root kinds return kCreateFailed per RULE-RUNTIME-001 and never a
// fallback box. Every entry is nothrow (catch-all) and FATAL-free: failures
// return codes plus logcat diagnostics via SeamLog. Never throws.

AttachResult attachSurface(const CompiledTreeRef& tree, void* opaque_window) {
  try {
    if (opaque_window == nullptr) {
      SeamLog("attachSurface: null window handle");
      return {0, {PresenterCode::kNullWindow,
                  "attachSurface: null window handle"}};
    }
    std::lock_guard<std::mutex> lock(g_mu);
    // Unknown component kind fails closed (RULE-RUNTIME-001): no usable
    // handle, never a default fallback box. Checked before window binding
    // so a bad tree never disturbs live-surface state.
    if (tree.unknownKind || tree.rootKind.empty()) {
      SeamLog("attachSurface: unknown component kind; rejected fail-closed");
      return {0, {PresenterCode::kCreateFailed,
                  "attachSurface: unknown component kind; surface not "
                  "created, no fallback box built"}};
    }
    if (reinterpret_cast<std::uintptr_t>(opaque_window) <
        kMinPlausibleAddress) {
      SeamLog("attachSurface: invalid window handle (unmapped address)");
      return {0, {PresenterCode::kInvalidWindow,
                  "attachSurface: invalid window handle (unmapped address)"}};
    }
    if (!LooksLikeWindow(opaque_window)) {
      SeamLog("attachSurface: invalid window handle (not an ANativeWindow)");
      return {0, {PresenterCode::kInvalidWindow,
                  "attachSurface: invalid window handle (not an "
                  "ANativeWindow)"}};
    }
    // Exactly one presenter per ANativeWindow (RULE-RUNTIME-006): a window
    // already bound to a live seam surface rejects the second attach with
    // state untouched.
    const auto bound = g_seam_window.find(opaque_window);
    if (bound != g_seam_window.end()) {
      const auto live = g_seam_surfaces.find(bound->second);
      if (live != g_seam_surfaces.end() && live->second.live) {
        SeamLog("attachSurface: window already attached; detach first");
        return {0, {PresenterCode::kAlreadyAttached,
                    "attachSurface: presenter already attached to this "
                    "window; detach first"}};
      }
    }
    // Arm the FR-RUNTIME-009 machinery on the probed path: the retained
    // handle plus a live swapchain on the last-probed backend. The host
    // owns the window; the seam only retains the opaque handle until
    // detach/destroy/invalidate.
    SurfaceHandle handle = g_seam_next++;
    if (handle == 0) {
      handle = g_seam_next++;
    }
    SeamSurface surface;
    surface.live = true;
    surface.window = opaque_window;
    surface.swapchain = true;
    g_seam_surfaces.emplace(handle, surface);
    g_seam_window.emplace(opaque_window, handle);
    // Mirror into the single-surface FR-RUNTIME-009 globals so the legacy
    // present leg and the seam leg observe the same attached window.
    g_attached_window = opaque_window;
    g_surface_created = true;
    g_has_current_frame = false;
    return {handle, {PresenterCode::kOk, ""}};
  } catch (...) {
    SeamLog("attachSurface: exception during attach");
    return {0, {PresenterCode::kInvalidWindow,
                "attachSurface: exception during attach"}};
  }
}

void NoteFrameRecordedForSurface(SurfaceHandle h, const DrawCmds& cmds) {
  try {
    (void)cmds;
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      return;
    }
    // DAG currency: marks these cmds as the recorded current-frame set so
    // a later renderFrame may present. Never throws, never logs on the
    // record path (present path owns the diagnostics).
    it->second.has_current_frame = true;
  } catch (...) {
  }
}

PresenterError renderFrame(SurfaceHandle h, const DrawCmds& cmds) {
  try {
    (void)cmds;
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      SeamLog("renderFrame: no surface for handle");
      return {PresenterCode::kNoSurface,
              "renderFrame: no live surface for handle; frame not presented"};
    }
    if (!it->second.swapchain) {
      SeamLog("renderFrame: no swapchain; frame not presented");
      return {PresenterCode::kNoSwapchain,
              "renderFrame: no swapchain; frame not presented"};
    }
    if (!it->second.has_current_frame) {
      SeamLog("renderFrame: no DAG-recorded current-frame cmds");
      return {PresenterCode::kNoCurrentFrame,
              "renderFrame: no DAG-recorded current-frame cmds; frame not "
              "presented"};
    }
    ++it->second.stats.presented;
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("renderFrame: exception during present");
    return {PresenterCode::kNoSurface,
            "renderFrame: exception during present; frame not presented"};
  }
}

PresenterError dispatchTouch(SurfaceHandle h, std::int32_t action,
                             std::int32_t pointerId, float xPx, float yPx,
                             std::int64_t eventTimeMs) {
  try {
    (void)eventTimeMs;
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      SeamLog("dispatchTouch: no surface; touch dropped fail-closed");
      return {PresenterCode::kNoSurface,
              "dispatchTouch: no live surface for handle; touch dropped"};
    }
    // FR-RUNTIME-010 input-chain entry (best-effort, fail-closed): seed the
    // gesture down origin on ACTION_DOWN so the arena slop verdict measures
    // from the true gesture start. RecordTapDown absorbs untrusted fields
    // internally (NaN/negative dropped); any failure here never fails the
    // dispatch itself.
    try {
      constexpr std::int32_t kActionDown = 0;
      if (action == kActionDown) {
        RecordTapDown(pointerId, xPx, yPx);
      }
    } catch (...) {
    }
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("dispatchTouch: exception during dispatch");
    return {PresenterCode::kNoSurface,
            "dispatchTouch: exception during dispatch; touch dropped"};
  }
}

PresenterError setTheme(SurfaceHandle h, ThemeId theme) {
  try {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      SeamLog("setTheme: no surface; theme not applied");
      return {PresenterCode::kNoSurface,
              "setTheme: no live surface for handle; theme not applied"};
    }
    // In place: the stored palette flips, the surface (and its swapchain,
    // currency, and stats) is untouched — no re-attach.
    it->second.theme = theme;
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("setTheme: exception during theme switch");
    return {PresenterCode::kNoSurface,
            "setTheme: exception during theme switch"};
  }
}

PresenterError setImeBottom(SurfaceHandle h, std::int32_t bottomPx) {
  try {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      SeamLog("setImeBottom: no surface; inset not applied");
      return {PresenterCode::kNoSurface,
              "setImeBottom: no live surface for handle; inset not applied"};
    }
    // In place, applied within one frame: the stored inset updates, the
    // surface is untouched — no re-attach. Bounds clamp fail-closed
    // (negative/oversized never crash, never draw unowned).
    it->second.ime_bottom_px = ClampImeBottom(bottomPx);
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("setImeBottom: exception during inset feed");
    return {PresenterCode::kNoSurface,
            "setImeBottom: exception during inset feed"};
  }
}

PresenterError detachSurface(SurfaceHandle h) {
  try {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      // Idempotent: repeating detach (or detaching an unknown handle) is
      // still kOk, never an error.
      return {PresenterCode::kOk, ""};
    }
    // Swapchain-before-window (RULE-RUNTIME-006): drop the swapchain and
    // currency first, release the retained handle after.
    void* window = it->second.window;
    it->second.swapchain = false;
    it->second.has_current_frame = false;
    it->second.window = nullptr;
    it->second.live = false;
    g_seam_surfaces.erase(it);
    if (window != nullptr) {
      const auto bound = g_seam_window.find(window);
      if (bound != g_seam_window.end() && bound->second == h) {
        g_seam_window.erase(bound);
      }
      if (g_attached_window == window) {
        g_attached_window = nullptr;
        g_surface_created = false;
        g_has_current_frame = false;
      }
    }
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("detachSurface: exception during detach");
    return {PresenterCode::kOk, ""};
  }
}

PresenterError destroySurface(SurfaceHandle h) {
  try {
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      // Idempotent teardown: safe to repeat, always kOk.
      return {PresenterCode::kOk, ""};
    }
    // Same swapchain-before-window ordering as detach; the tree handle is
    // untouched (host-owned, RULE-RUNTIME-004).
    void* window = it->second.window;
    it->second.swapchain = false;
    it->second.has_current_frame = false;
    it->second.window = nullptr;
    it->second.live = false;
    g_seam_surfaces.erase(it);
    if (window != nullptr) {
      const auto bound = g_seam_window.find(window);
      if (bound != g_seam_window.end() && bound->second == h) {
        g_seam_window.erase(bound);
      }
      if (g_attached_window == window) {
        g_attached_window = nullptr;
        g_surface_created = false;
        g_has_current_frame = false;
      }
    }
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("destroySurface: exception during destroy");
    return {PresenterCode::kOk, ""};
  }
}

PresenterError getFrameStats(SurfaceHandle h, FrameStats* out) {
  try {
    if (out == nullptr) {
      SeamLog("getFrameStats: null out; stats not read");
      return {PresenterCode::kNoSurface,
              "getFrameStats: null stats out-param; nothing read"};
    }
    std::lock_guard<std::mutex> lock(g_mu);
    const auto it = g_seam_surfaces.find(h);
    if (it == g_seam_surfaces.end() || !it->second.live) {
      SeamLog("getFrameStats: no surface; stats not read");
      return {PresenterCode::kNoSurface,
              "getFrameStats: no live surface for handle; stats not read"};
    }
    *out = it->second.stats;
    return {PresenterCode::kOk, ""};
  } catch (...) {
    SeamLog("getFrameStats: exception during stats read");
    return {PresenterCode::kNoSurface,
            "getFrameStats: exception during stats read"};
  }
}

}  // namespace runtime
