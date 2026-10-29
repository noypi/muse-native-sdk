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
  // T5 paced present (FR-RUNTIME-009 / AC-RUNTIME-031): missed vsync and
  // fail-closed pacing errors. Appended so T1–T4 codes are untouched.
  kDeadlineMissed = 10,
  kPacingError = 11,
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

// Render-ahead bound imported from the FR-GRAPHICS-007 pacing policy.
// Named-constant import, not invented policy: PresentPaced admits at most
// kMaxRenderAhead presents inside one choreographer deadline window; the
// excess is clamped (kDeadlineMissed/kPacingError) so no unbounded
// render-ahead occurs.
constexpr int kMaxRenderAhead = 2;

// Choreographer vsync deadline. On-device this mirrors the AChoreographer
// vsync timestamp consumed via the Swappy / ASurfaceControl paced path; on
// non-Android (host) builds it is an in-process fake carrying explicit
// nanosecond values — the OS clock is never read and no real
// AChoreographer/Swappy calls occur on the host lane.
struct ChoreographerDeadline {
  long long deadline_ns = 0;
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

// Paced present on AChoreographer deadlines (FR-RUNTIME-009 / AC-RUNTIME-031,
// manual gallery TC-031). On-device the frame rides the Swappy /
// ASurfaceControl paced path; on non-Android (host) builds the deadline is an
// in-process fake — no real AChoreographer/Swappy calls occur on the host
// lane. Fail-closed semantics; the return set is closed to
// kOk | kDeadlineMissed | kPacingError:
//   - no live swapchain (never attached, never created, backgrounded, or
//     already destroyed)       -> kPacingError (+ diagnostic), not presented.
//   - already-expired deadline -> kDeadlineMissed (+ diagnostic), never
//     presented as on-time.
//   - more than kMaxRenderAhead presents inside one deadline window
//                              -> kPacingError (+ diagnostic), excess clamped
//     so no unbounded render-ahead occurs.
//   - otherwise                -> kOk (frame paced-presented on the deadline).
PresenterError PresentPaced(const DrawCmds& cmds, ChoreographerDeadline deadline);

}  // namespace runtime
