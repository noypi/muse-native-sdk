#pragma once

#include <string>
#include <vector>

#include "runtime/renderer.h"

namespace runtime {

// Frame command list recorded by the DAG for the current frame. Currency is
// DAG-recorded presenter state, never inferred from list shape: a hand-built
// list — empty or not — that was never DAG-recorded is not current.
using DrawCmds = std::vector<DrawCmd>;

// Presenter result following the BaselineCheck convention: a code plus a
// human-readable diagnostic (non-empty on every error).
enum class PresenterCode {
  kOk = 0,
  kNullWindow = 1,
  kInvalidWindow = 2,
  kAlreadyAttached = 3,
  kMissingExtension = 4,
  kCreateFailed = 5,
  kNoSwapchain = 6,
  kNoCurrentFrame = 7,
  // T4 lifecycle owns the surface-destroyed path; declared here so the
  // minimal OnSurfaceDestroyed below can fail closed with no surface.
  kNoSurface = 8,
  kRecreateFailed = 9,
};

struct PresenterError {
  PresenterCode code = PresenterCode::kOk;
  std::string message;
};

// Probed backend carried from FR-GRAPHICS-003. Never assumed: every
// CreateSurfaceAndSwapchain call reads android_surface_ext_present as passed.
enum class ProbeBackend {
  kVulkan11Emulated = 0,
  kGles32Window = 1,
};

struct BackendProbe {
  ProbeBackend backend = ProbeBackend::kVulkan11Emulated;
  bool android_surface_ext_present = false;
};

// Attach the JNI-carried ANativeWindow handle, carried opaquely as void*.
// The handle is never dereferenced as a View. Fail-closed semantics:
//   - nullptr              -> kNullWindow (+ diagnostic), nothing retained.
//   - non-null, non-window -> kInvalidWindow (+ diagnostic), nothing retained.
//   - already attached     -> kAlreadyAttached (+ diagnostic), state untouched.
//   - otherwise            -> kOk, handle retained until DetachSurface().
PresenterError AttachSurface(void* opaque_window);

// Release the attached handle. Idempotent: safe to call with nothing
// attached (returns kOk); never fails.
PresenterError DetachSurface();

// Create the VkSurfaceKHR + swapchain on the probed backend path.
// Fail-closed semantics; never presents silently:
//   - probe without VK_KHR_android_surface -> kMissingExtension (+ diagnostic),
//     on every backend path (probe result is never assumed).
//   - no attached surface                  -> kCreateFailed (+ diagnostic).
//   - otherwise                            -> kOk (surface+swapchain created
//     on the probed backend; no View creation).
PresenterError CreateSurfaceAndSwapchain(const BackendProbe& probe);

// Present the DAG-recorded current-frame DrawCmds (FR-RUNTIME-009 /
// AC-RUNTIME-029, manual gallery TC-029). Fail-closed semantics; an
// empty/stale acquisition without current-frame DrawCmds is never presented:
//   - no live swapchain                  -> kNoSwapchain (+ diagnostic).
//   - cmds is not the DAG-recorded
//     current-frame set                  -> kNoCurrentFrame (+ diagnostic).
//   - otherwise (cmds is the DAG-recorded current-frame set on a live
//     swapchain)                         -> kOk (frame presented).
PresenterError PresentFrame(const DrawCmds& cmds);

// Swapchain lifecycle (FR-RUNTIME-009 / AC-RUNTIME-030, RULE-RUNTIME-006).
// Teardown destroys the swapchain before the window; the host tree handle is
// untouched (no re-instantiation); no dangling surface is left behind.
//   - OnSurfaceDestroyed: no live surface -> kNoSurface (+ diagnostic);
//     otherwise tears down swapchain-then-window, drops current-frame
//     currency, and returns kOk.
//   - OnSurfaceRecreated: nullptr -> kNoSurface (+ diagnostic), nothing
//     installed; invalid handle -> kNoSurface or kRecreateFailed (+
//     diagnostic), nothing installed; otherwise attaches the fresh window and
//     re-creates surface+swapchain -> kOk.
//   - OnBackground: no live surface -> kNoSurface (+ diagnostic); otherwise
//     suspends presentation (swapchain retained for foreground) -> kOk.
//   - OnForeground: no live surface -> kNoSurface (+ diagnostic); otherwise
//     resumes presentation -> kOk.
PresenterError OnSurfaceDestroyed();
PresenterError OnSurfaceRecreated(void* opaque_window);
PresenterError OnBackground();
PresenterError OnForeground();

}  // namespace runtime
