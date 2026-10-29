#pragma once

#include <string>

namespace runtime {

// Presenter result following the BaselineCheck convention: a code plus a
// human-readable diagnostic (non-empty on every error).
enum class PresenterCode {
  kOk = 0,
  kNullWindow = 1,
  kInvalidWindow = 2,
  kAlreadyAttached = 3,
  kMissingExtension = 4,
  kCreateFailed = 5,
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

}  // namespace runtime
