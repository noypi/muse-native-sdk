#pragma once

#include <cstdint>
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

// Ownership contract (RULE-RUNTIME-004): the host owns the ANativeWindow.
// The presenter retains the opaque handle only between AttachSurface (or
// OnSurfaceRecreated) and DetachSurface / OnSurfaceDestroyed /
// InvalidateWindow. The host MUST call DetachSurface, OnSurfaceDestroyed, or
// InvalidateWindow before freeing the window; presenting after the host has
// freed the window without such a call is a contract violation. The presenter
// never dereferences the handle as a View, never frees it, and never
// outlives the host's invalidation call. OnSurfaceRecreated intentionally
// performs a legal swap (unlike AttachSurface's kAlreadyAttached guard):
// config-change delivers a fresh window and the presenter replaces the
// retained handle, re-creating surface+swapchain on it.
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

// Present the DAG-recorded current-frame component set (FR-GRAPHICS-015 /
// AC-GRAPHICS-042). Component-record path beside the oracle path above;
// same fail-closed shape and same currency via NoteFrameRecorded() as
// PresentFrame: no new IDL, no Attach/Detach/swapchain semantics change.
//   - no live swapchain                  -> kNoSwapchain (+ diagnostic).
//   - cmds is not the DAG-recorded
//     current-frame set                  -> kNoCurrentFrame (+ diagnostic).
//   - otherwise (cmds is the DAG-recorded current-frame component set on a
//     live swapchain)                    -> kOk (frame presented).
// Never throws.
inline PresenterError PresentRealComponents(const std::vector<DrawCmd>& cmds) {
  try {
    return PresentFrame(cmds);
  } catch (...) {
    return {PresenterCode::kNoCurrentFrame,
            "PresentRealComponents: exception during present; frame not "
            "presented"};
  }
}

// DAG currency seam (test-owned). The DAG record path calls
// NoteFrameRecorded() after recording the current-frame DrawCmds; only then
// may PresentFrame return kOk. Hand-built lists that were never DAG-recorded
// always fail closed with kNoCurrentFrame, whether empty or stale. Cleared by
// DetachSurface()/OnSurfaceDestroyed()/OnSurfaceRecreated()/InvalidateWindow().
void NoteFrameRecorded();

// Explicit host-invalidate hook: call before freeing the window without going
// through DetachSurface/OnSurfaceDestroyed. If `opaque_window` matches the
// retained handle (or is nullptr, meaning "any"), the presenter drops the
// retained handle, the swapchain, and current-frame currency so no dangling
// handle can report a false-kOk create or be presented on.
PresenterError InvalidateWindow(void* opaque_window);

// Last probed backend recorded by CreateSurfaceAndSwapchain for downstream
// use (the stub creates on either backend path; recording keeps the probed
// choice observable instead of ignored).
ProbeBackend LastProbedBackend();

// Host-lifecycle wiring (plan T4 Detail): each drives the FR-RUNTIME-005
// AC-011/013 host transition AND the matching presenter teardown/recreate so
// device config-change / background / foreground cannot leave the swapchain
// in a stale state. Defined in src/host.cpp; declared here (instead of
// host.h) so host.h stays a header-only seam. Each returns the presenter
// step's error; the host step itself never fails.
struct Host;
PresenterError HostOnSurfaceDestroyed(Host& host);
PresenterError HostOnSurfaceRecreated(Host& host, void* opaque_window);
PresenterError HostOnBackground(Host& host);
PresenterError HostOnForeground(Host& host);
PresenterError HostOnConfigChange(Host& host, void* fresh_window);

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

// Single Surface host seam (FR-RUNTIME-011 / AC-RUNTIME-034). Component-record
// path beside the oracle path above; follows the same PresenterError /
// PresenterCode convention. DrawCmds above is reused directly (no second list
// type); CompiledTreeRef is new here — tree.h carries CompiledNode/RuntimeTree
// input fixtures with no rootKind/unknownKind fields, so there is no existing
// type to alias for the evaluated-tree reference. Definitions land in T2;
// this item declares the seam only. No gallery term appears in any name here.
// Opaque integer handle; 0 is never a live surface.
using SurfaceHandle = std::uint64_t;

// Evaluated-tree reference carried into attachSurface. unknownKind marks a
// root kind the component set cannot build; attach then fails closed with
// kCreateFailed and never builds a fallback box.
struct CompiledTreeRef {
  std::string rootKind;
  bool unknownKind = false;
};

enum class ThemeId {
  kLight = 0,
  kDark = 1,
};

// Lifetime counters observable without device-only calls.
struct FrameStats {
  std::uint64_t presented = 0;
  std::uint64_t dropped = 0;
  std::uint64_t deadlineMissed = 0;
};

// attachSurface outcome: a handle plus the fail-closed error. On any error
// the handle is not usable; renderFrame on it is rejected.
struct AttachResult {
  SurfaceHandle handle = 0;
  PresenterError error;
};

// Attach the evaluated tree to the opaque window handle (never dereferenced
// as a View, never freed, retained only until detach/destroy/invalidate).
// Fail-closed: nullptr -> kNullWindow; non-null non-window -> kInvalidWindow;
// already bound -> kAlreadyAttached (state untouched); unknown root kind ->
// kCreateFailed (no usable handle, never a fallback box); else kOk.
AttachResult attachSurface(const CompiledTreeRef& tree, void* opaque_window);

// Present the DAG-recorded current-frame cmds for the surface.
// kOk | kNoSwapchain | kNoCurrentFrame; unknown-kind handles are rejected
// here as well (kCreateFailed/kNoSurface/kNoSwapchain/kNoCurrentFrame).
PresenterError renderFrame(SurfaceHandle h, const DrawCmds& cmds);

// Raw touch fields (action, pointer id, x/y in px, event time in ms); no
// View object crosses. kOk on a live surface, else kNoSurface.
PresenterError dispatchTouch(SurfaceHandle h, std::int32_t action,
                             std::int32_t pointerId, float xPx, float yPx,
                             std::int64_t eventTimeMs);

// In-place theme switch; no re-attach. kOk live, else kNoSurface.
PresenterError setTheme(SurfaceHandle h, ThemeId theme);

// In-place bottom inset in px, applied within one frame; no re-attach.
// kOk live, else kNoSurface.
PresenterError setImeBottom(SurfaceHandle h, std::int32_t bottomPx);

// Idempotent release; always kOk, safe to repeat.
PresenterError detachSurface(SurfaceHandle h);

// Idempotent teardown; always kOk, safe to repeat.
PresenterError destroySurface(SurfaceHandle h);

// Read lifetime counters; kOk live with *out filled, else kNoSurface.
PresenterError getFrameStats(SurfaceHandle h, FrameStats* out);

// DAG currency helper for the surface path (test-owned): marks cmds as the
// recorded current-frame set for h so a later renderFrame may return kOk.
void NoteFrameRecordedForSurface(SurfaceHandle h, const DrawCmds& cmds);

}  // namespace runtime
