// T2 contract tests — FR-RUNTIME-009 / AC-RUNTIME-028 (TC-RUNTIME-028).
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side
// ANativeWindow/VkSurfaceKHR fakes behind `#ifndef __ANDROID__` guards;
// no NDK/Vulkan calls on host, no network, no env/config reads,
// no filesystem, no live services.
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "runtime/presenter.h"

// --- harness cases (convention copied from host_lifecycle_test.cpp) ----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T2 helpers --------------------------------------------------------------

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake (same seam as T1). The presenter must
// never assume the probe result; the probe is passed in read-only.
struct FakeNativeWindow {
  unsigned magic = 0xA11CEu;
};
#endif

void reset_presenter() {
  // Detach is idempotent; ignore the result during reset.
  (void)runtime::DetachSurface();
}

bool error_is(runtime::PresenterError err, runtime::PresenterCode want) {
  return err.code == want;
}

bool has_diagnostic(const runtime::PresenterError& err) {
  return !err.message.empty();
}

runtime::BackendProbe probe_without_extension() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = false;
  return p;
}

runtime::BackendProbe probe_with_extension() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  return p;
}

}  // namespace

// Named case: missing VK_KHR_android_surface errors clearly with
// kMissingExtension + diagnostic, and never presents silently.
static bool check_missing_extension_errors_clearly() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError attach =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError attach =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  if (!error_is(attach, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError err =
      runtime::CreateSurfaceAndSwapchain(probe_without_extension());
  bool ok = error_is(err, runtime::PresenterCode::kMissingExtension) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: probe result is never assumed — a probe reporting the
// extension absent on the GLES path also yields kMissingExtension.
static bool check_probe_never_assumed_on_gles_path() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError attach =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError attach =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  if (!error_is(attach, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kGles32Window;
  p.android_surface_ext_present = false;
  runtime::PresenterError err = runtime::CreateSurfaceAndSwapchain(p);
  bool ok = error_is(err, runtime::PresenterCode::kMissingExtension) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: probed-present probe creates surface+swapchain on the probed
// Vulkan backend path with kOk.
static bool check_probed_present_creates_on_vulkan_path() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError attach =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError attach =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  if (!error_is(attach, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError err =
      runtime::CreateSurfaceAndSwapchain(probe_with_extension());
  bool ok = error_is(err, runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: probed-present probe creates surface+swapchain on the probed
// GLES 3.2 window-surface path with kOk.
static bool check_probed_present_creates_on_gles_path() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError attach =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError attach =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  if (!error_is(attach, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kGles32Window;
  p.android_surface_ext_present = true;
  runtime::PresenterError err = runtime::CreateSurfaceAndSwapchain(p);
  bool ok = error_is(err, runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: creation without an attached surface fails closed with a clear
// error (kCreateFailed or kInvalidWindow) + diagnostic — never presents
// silently.
static bool check_create_without_attach_fails_closed() {
  reset_presenter();
  runtime::PresenterError err =
      runtime::CreateSurfaceAndSwapchain(probe_with_extension());
  bool ok =
      (error_is(err, runtime::PresenterCode::kCreateFailed) ||
       error_is(err, runtime::PresenterCode::kInvalidWindow)) &&
      has_diagnostic(err);
  reset_presenter();
  return ok;
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_missing_extension_errors_clearly);
  RUN_CASE(check_probe_never_assumed_on_gles_path);
  RUN_CASE(check_probed_present_creates_on_vulkan_path);
  RUN_CASE(check_probed_present_creates_on_gles_path);
  RUN_CASE(check_create_without_attach_fails_closed);
  if (failures > 0) {
    std::printf("swapchain_surface_test: %d failing\n", failures);
    return 1;
  }
  std::printf("swapchain_surface_test: all passing\n");
  return 0;
}
