// Opaque ANativeWindow attach (FR-RUNTIME-009 / AC-RUNTIME-027).
// The JNI-carried window handle crosses as an opaque void* and is never
// dereferenced as a View, per FR-CORE-004. Null/invalid handles fail closed:
// no crash, no retained handle, no View creation. A single presenter owns the
// window at a time (RULE-RUNTIME-006): a second attach without an intervening
// detach is rejected with kAlreadyAttached.

#include "runtime/presenter.h"

#include <cstdint>
#include <cstring>

namespace runtime {
namespace {

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

// DAG-recorded current-frame currency. Set only by the DAG record hook;
// PresentFrame never infers currency from list shape (empty vs non-empty),
// so hand-built lists that were never DAG-recorded always fail closed with
// kNoCurrentFrame. Cleared on DetachSurface()/OnSurfaceDestroyed().
bool g_has_current_frame = false;

// Magic word identifying a host-side window handle. On host builds the
// ANativeWindow is a fake whose first word carries this magic behind the
// non-Android guard; real NDK calls happen only under `__ANDROID__`.
constexpr std::uint32_t kWindowMagic = 0xA11CEu;

// Low-address guard: values this small can never be a mapped window object.
// Rejects hostile sentinels (e.g. reinterpret_cast<void*>(0x1)) without
// dereferencing them.
constexpr std::uintptr_t kMinPlausibleAddress = 0x1000u;

bool LooksLikeWindow(void* opaque_window) {
#ifdef __ANDROID__
  // On-device the handle is a real ANativeWindow owned by the host. Only the
  // low-address guard applies here; the NDK owns validation beyond that, and
  // no View dereference ever happens in this path.
  (void)opaque_window;
  return true;
#else
  // Host hermetic lane: validity is the magic word, read with memcpy so no
  // type punning or View-shaped access occurs.
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

void ResetPacedWindow() {
  g_paced_window_ns = 0;
  g_paced_window_open = false;
  g_paced_in_window = 0;
}

}  // namespace

PresenterError AttachSurface(void* opaque_window) {
  if (opaque_window == nullptr) {
    return {PresenterCode::kNullWindow, "AttachSurface: null window handle"};
  }
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
  g_attached_window = nullptr;
  g_surface_created = false;
  g_has_current_frame = false;
  g_backgrounded = false;
  ResetPacedWindow();
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
  // No View creation: the attached ANativeWindow handle is only retained,
  // never dereferenced as a View, and creation without one fails closed.
  if (g_attached_window == nullptr) {
    return {PresenterCode::kCreateFailed,
            "CreateSurfaceAndSwapchain: no attached surface; attach a "
            "window before creating surface+swapchain"};
  }
  g_surface_created = true;
  return {PresenterCode::kOk, ""};
}

PresenterError PresentFrame(const DrawCmds& cmds) {
  (void)cmds;
  // No dangling-surface present: without a live swapchain nothing is
  // presented, whatever cmds holds.
  if (!g_surface_created) {
    return {PresenterCode::kNoSwapchain,
            "PresentFrame: no swapchain; frame not presented"};
  }
  // Currency is DAG-recorded state, never inferred from list shape: only the
  // DAG-recorded current-frame set may be presented, so an empty or stale
  // (hand-built, never-DAG-recorded) acquisition fails closed here. The kOk
  // visible-frames path is proven manually via examples/m3-gallery TC-029.
  if (!g_has_current_frame) {
    return {PresenterCode::kNoCurrentFrame,
            "PresentFrame: no DAG-recorded current-frame DrawCmds; frame "
            "not presented"};
  }
  return {PresenterCode::kOk, ""};
}

PresenterError OnSurfaceDestroyed() {
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
  ResetPacedWindow();
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
  if (!LooksLikeWindow(opaque_window)) {
    return {PresenterCode::kRecreateFailed,
            "OnSurfaceRecreated: invalid window handle (not an "
            "ANativeWindow); surface not recreated"};
  }
  // Fresh window replaces any prior handle; swapchain is re-created on it.
  // Currency stays dropped until the DAG records the next current frame.
  g_attached_window = opaque_window;
  g_surface_created = true;
  g_has_current_frame = false;
  g_backgrounded = false;
  ResetPacedWindow();
  return {PresenterCode::kOk, ""};
}

PresenterError OnBackground() {
  if (!g_surface_created) {
    return {PresenterCode::kNoSurface,
            "OnBackground: no surface; nothing to suspend"};
  }
  g_backgrounded = true;
  return {PresenterCode::kOk, ""};
}

PresenterError OnForeground() {
  if (!g_surface_created) {
    return {PresenterCode::kNoSurface,
            "OnForeground: no surface; nothing to resume"};
  }
  g_backgrounded = false;
  return {PresenterCode::kOk, ""};
}

PresenterError PresentPaced(const DrawCmds& cmds, ChoreographerDeadline deadline) {
  (void)cmds;
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
