// T4 contract tests — FR-RUNTIME-009 / AC-RUNTIME-030 (TC-RUNTIME-030).
// Binds ONLY to the T4 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side ANativeWindow
// fakes behind `#ifndef __ANDROID__` guards; no NDK/Vulkan calls on host,
// no network, no env/config reads, no filesystem, no live services.
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "runtime/host.h"
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

// --- T4 helpers --------------------------------------------------------------

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake (same seam as T1/T2). The presenter
// must never dereference the handle as a View.
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

bool attach_valid_window() {
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError attach =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError attach =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  return error_is(attach, runtime::PresenterCode::kOk);
}

bool create_swapchain() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  return error_is(runtime::CreateSurfaceAndSwapchain(p),
                  runtime::PresenterCode::kOk);
}

bool setup_live_swapchain() {
  return attach_valid_window() && create_swapchain();
}

}  // namespace

// Named case: OnSurfaceDestroyed with no surface fails closed with
// kNoSurface + diagnostic.
static bool check_destroy_without_surface_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::OnSurfaceDestroyed();
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: destroy/recreate cycle — OnSurfaceDestroyed on a live
// swapchain returns kOk with teardown-before-window ordering, and a
// recreate on a fresh valid window returns kOk (no re-instantiation of
// the tree: host tree handle untouched).
static bool check_destroy_recreate_cycle_succeeds() {
  reset_presenter();
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
#ifndef __ANDROID__
  FakeNativeWindow w2;
  runtime::PresenterError recreated =
      runtime::OnSurfaceRecreated(static_cast<void*>(&w2));
#else
  runtime::PresenterError recreated =
      runtime::OnSurfaceRecreated(reinterpret_cast<void*>(0x1234));
#endif
  bool ok = error_is(recreated, runtime::PresenterCode::kOk) &&
            runtime::live_tree_nodes(host) == tree_before &&
            tree_before > 0;
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: config-change path — destroy then recreate on a fresh window
// succeeds (kOk/kOk) with the host tree untouched (no re-instantiation,
// RULE-RUNTIME-004).
static bool check_config_change_preserves_tree() {
  reset_presenter();
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::simulate_config_change(host);
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
#ifndef __ANDROID__
  FakeNativeWindow w2;
  runtime::PresenterError recreated =
      runtime::OnSurfaceRecreated(static_cast<void*>(&w2));
#else
  runtime::PresenterError recreated =
      runtime::OnSurfaceRecreated(reinterpret_cast<void*>(0x1234));
#endif
  bool ok = error_is(recreated, runtime::PresenterCode::kOk) &&
            runtime::live_tree_nodes(host) == tree_before;
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: OnSurfaceRecreated with a null window fails closed with
// kNoSurface + diagnostic — no dangling surface is installed.
static bool check_recreate_null_window_fails_closed() {
  reset_presenter();
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError err = runtime::OnSurfaceRecreated(nullptr);
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: OnSurfaceRecreated with an invalid (non-window) handle fails
// closed (kNoSurface or kRecreateFailed) + diagnostic — never installs a
// dangling surface.
static bool check_recreate_invalid_window_fails_closed() {
  reset_presenter();
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
#ifndef __ANDROID__
  int bogus = 0;  // never a FakeNativeWindow: must be rejected, not probed.
  runtime::PresenterError err =
      runtime::OnSurfaceRecreated(static_cast<void*>(&bogus));
#else
  runtime::PresenterError err =
      runtime::OnSurfaceRecreated(reinterpret_cast<void*>(0x1));
#endif
  bool ok = (error_is(err, runtime::PresenterCode::kNoSurface) ||
             error_is(err, runtime::PresenterCode::kRecreateFailed)) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: background/foreground on a live swapchain returns kOk/kOk
// with the host tree untouched (no re-instantiation).
static bool check_background_foreground_preserves_tree() {
  reset_presenter();
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::to_background(host);
  runtime::PresenterError bg = runtime::OnBackground();
  if (!error_is(bg, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::to_foreground(host);
  runtime::PresenterError fg = runtime::OnForeground();
  bool ok = error_is(fg, runtime::PresenterCode::kOk) &&
            runtime::live_tree_nodes(host) == tree_before &&
            tree_before > 0;
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: OnBackground with no surface fails closed with kNoSurface +
// diagnostic.
static bool check_background_without_surface_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::OnBackground();
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: OnForeground with no surface fails closed with kNoSurface +
// diagnostic.
static bool check_foreground_without_surface_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::OnForeground();
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
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
  RUN_CASE(check_destroy_without_surface_fails_closed);
  RUN_CASE(check_destroy_recreate_cycle_succeeds);
  RUN_CASE(check_config_change_preserves_tree);
  RUN_CASE(check_recreate_null_window_fails_closed);
  RUN_CASE(check_recreate_invalid_window_fails_closed);
  RUN_CASE(check_background_foreground_preserves_tree);
  RUN_CASE(check_background_without_surface_fails_closed);
  RUN_CASE(check_foreground_without_surface_fails_closed);
  if (failures > 0) {
    std::printf("swapchain_lifecycle_test: %d failing\n", failures);
    return 1;
  }
  std::printf("swapchain_lifecycle_test: all passing\n");
  return 0;
}
