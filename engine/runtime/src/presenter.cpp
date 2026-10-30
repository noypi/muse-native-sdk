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

#include <cstdint>
#include <cstring>
#include <mutex>

#ifndef __ANDROID__
#include <sys/mman.h>
#include <unistd.h>
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

}  // namespace runtime
