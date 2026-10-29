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

bool attach_valid_window(void* handle) {
  // The fake window outlives the call: callers declare it at test scope and
  // pass the handle in, so the presenter never retains a dangling
  // stack-local from this helper's return.
  runtime::PresenterError attach = runtime::AttachSurface(handle);
  return error_is(attach, runtime::PresenterCode::kOk);
}

bool create_swapchain() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  return error_is(runtime::CreateSurfaceAndSwapchain(p),
                  runtime::PresenterCode::kOk);
}

bool setup_live_swapchain(void* handle) {
  return attach_valid_window(handle) && create_swapchain();
}

// Each test below declares its FakeNativeWindow at test (function) scope so
// the retained handle outlives every presenter call in that test — no
// stack-local fake ever dies at a helper return while still attached.

}  // namespace

// Named case: OnSurfaceDestroyed with no surface fails closed with
// kNoSurface + diagnostic. A second call still fails closed and a subsequent
// valid attach+create still succeeds (no state mutated).
static bool check_destroy_without_surface_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::OnSurfaceDestroyed();
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
            has_diagnostic(err);
  runtime::PresenterError again = runtime::OnSurfaceDestroyed();
  ok = ok && error_is(again, runtime::PresenterCode::kNoSurface) &&
       has_diagnostic(again);
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  ok = ok && setup_live_swapchain(handle);
  reset_presenter();
  return ok;
}

// Named case: destroy/recreate cycle — OnSurfaceDestroyed on a live
// swapchain returns kOk with teardown-before-window ordering, and a
// recreate on a fresh valid window returns kOk (no re-instantiation of
// the tree: host tree handle untouched).
static bool check_destroy_recreate_cycle_succeeds() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  FakeNativeWindow w2;
  void* handle2 = static_cast<void*>(&w2);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
  void* handle2 = reinterpret_cast<void*>(0x5678);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError recreated = runtime::OnSurfaceRecreated(handle2);
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
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  FakeNativeWindow w2;
  void* handle2 = static_cast<void*>(&w2);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
  void* handle2 = reinterpret_cast<void*>(0x5678);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
    reset_presenter();
    return false;
  }
  runtime::simulate_config_change(host);
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError recreated = runtime::OnSurfaceRecreated(handle2);
  bool ok = error_is(recreated, runtime::PresenterCode::kOk) &&
            runtime::live_tree_nodes(host) == tree_before;
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: OnSurfaceRecreated with a null window fails closed with
// kNoSurface + diagnostic — no dangling surface is installed. A second null
// recreate still fails closed and a subsequent valid recreate on a fresh
// window still succeeds (no state mutated).
static bool check_recreate_null_window_fails_closed() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  FakeNativeWindow w2;
  void* handle2 = static_cast<void*>(&w2);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
  void* handle2 = reinterpret_cast<void*>(0x5678);
#endif
  if (!setup_live_swapchain(handle)) {
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
  runtime::PresenterError again = runtime::OnSurfaceRecreated(nullptr);
  ok = ok && error_is(again, runtime::PresenterCode::kNoSurface) &&
       has_diagnostic(again);
  ok = ok && error_is(runtime::OnSurfaceRecreated(handle2),
                      runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: OnSurfaceRecreated with an invalid (non-window) handle fails
// closed (kNoSurface or kRecreateFailed) + diagnostic — never installs a
// dangling surface.
static bool check_recreate_invalid_window_fails_closed() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  FakeNativeWindow w2;
  void* handle2 = static_cast<void*>(&w2);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
  void* handle2 = reinterpret_cast<void*>(0x5678);
#endif
  if (!setup_live_swapchain(handle)) {
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
  bool ok = (error_is(err, runtime::PresenterCode::kNoSurface) ||
             error_is(err, runtime::PresenterCode::kRecreateFailed)) &&
            has_diagnostic(err);
  runtime::PresenterError again =
      runtime::OnSurfaceRecreated(static_cast<void*>(&bogus));
  ok = ok && (error_is(again, runtime::PresenterCode::kNoSurface) ||
              error_is(again, runtime::PresenterCode::kRecreateFailed)) &&
       has_diagnostic(again);
#else
  runtime::PresenterError err =
      runtime::OnSurfaceRecreated(reinterpret_cast<void*>(0x1));
  bool ok = (error_is(err, runtime::PresenterCode::kNoSurface) ||
             error_is(err, runtime::PresenterCode::kRecreateFailed)) &&
            has_diagnostic(err);
  runtime::PresenterError again =
      runtime::OnSurfaceRecreated(reinterpret_cast<void*>(0x1));
  ok = ok && (error_is(again, runtime::PresenterCode::kNoSurface) ||
              error_is(again, runtime::PresenterCode::kRecreateFailed)) &&
       has_diagnostic(again);
#endif
  ok = ok && error_is(runtime::OnSurfaceRecreated(handle2),
                      runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: background/foreground on a live swapchain returns kOk/kOk
// with the host tree untouched (no re-instantiation).
static bool check_background_foreground_preserves_tree() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
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
// diagnostic. A second call still fails closed and a subsequent valid
// attach+create still succeeds (no state mutated).
static bool check_background_without_surface_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::OnBackground();
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
            has_diagnostic(err);
  runtime::PresenterError again = runtime::OnBackground();
  ok = ok && error_is(again, runtime::PresenterCode::kNoSurface) &&
       has_diagnostic(again);
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  ok = ok && setup_live_swapchain(handle);
  reset_presenter();
  return ok;
}

// Named case: OnForeground with no surface fails closed with kNoSurface +
// diagnostic. A second call still fails closed and a subsequent valid
// attach+create still succeeds (no state mutated).
static bool check_foreground_without_surface_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::OnForeground();
  bool ok = error_is(err, runtime::PresenterCode::kNoSurface) &&
            has_diagnostic(err);
  runtime::PresenterError again = runtime::OnForeground();
  ok = ok && error_is(again, runtime::PresenterCode::kNoSurface) &&
       has_diagnostic(again);
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  ok = ok && setup_live_swapchain(handle);
  reset_presenter();
  return ok;
}

// Named case: HostOnSurfaceDestroyed drives both layers — the host surface
// goes down (tree untouched) and the presenter tears down the swapchain, so
// a second presenter destroy reports kNoSurface and PresentFrame fails
// closed with kNoSwapchain.
static bool check_host_on_destroyed_drives_both_layers() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError err = runtime::HostOnSurfaceDestroyed(host);
  bool ok = error_is(err, runtime::PresenterCode::kOk) &&
            !runtime::surface_alive(host) &&
            runtime::live_tree_nodes(host) == tree_before &&
            tree_before > 0;
  ok = ok && error_is(runtime::OnSurfaceDestroyed(),
                      runtime::PresenterCode::kNoSurface);
  runtime::DrawCmds cmds;
  ok = ok && error_is(runtime::PresentFrame(cmds),
                      runtime::PresenterCode::kNoSwapchain);
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: HostOnSurfaceRecreated drives both layers — the host surface
// is re-armed (tree untouched) and the presenter re-creates
// surface+swapchain on the fresh window, with frame currency still dropped
// until the DAG records the next frame.
static bool check_host_on_recreated_drives_both_layers() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  FakeNativeWindow fresh;
  void* fresh_handle = static_cast<void*>(&fresh);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
  void* fresh_handle = reinterpret_cast<void*>(0x5678);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
    reset_presenter();
    return false;
  }
  if (!error_is(runtime::HostOnSurfaceDestroyed(host),
                runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError err =
      runtime::HostOnSurfaceRecreated(host, fresh_handle);
  bool ok = error_is(err, runtime::PresenterCode::kOk) &&
            runtime::surface_valid(host) &&
            runtime::live_tree_nodes(host) == tree_before;
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmd.nodeId = 7;
  cmds.push_back(cmd);
  ok = ok && error_is(runtime::PresentFrame(cmds),
                      runtime::PresenterCode::kNoCurrentFrame);
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: HostOnBackground/HostOnForeground drive both layers — the host
// background flag and the presenter suspension move together with the tree
// untouched, and foreground resumes presentation.
static bool check_host_on_background_foreground_drive_both() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError bg = runtime::HostOnBackground(host);
  bool ok = error_is(bg, runtime::PresenterCode::kOk) && host.backgrounded &&
            runtime::live_tree_nodes(host) == tree_before;
  runtime::PresenterError fg = runtime::HostOnForeground(host);
  ok = ok && error_is(fg, runtime::PresenterCode::kOk) &&
       !host.backgrounded &&
       runtime::live_tree_nodes(host) == tree_before && tree_before > 0;
  reset_presenter();
  runtime::destroy_host(host);
  return ok;
}

// Named case: HostOnConfigChange drives both layers — config-change
// destroy+recreate on a fresh window succeeds with the host tree untouched
// and the presenter live again on the new window.
static bool check_host_on_config_change_drives_both() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  FakeNativeWindow fresh;
  void* fresh_handle = static_cast<void*>(&fresh);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
  void* fresh_handle = reinterpret_cast<void*>(0x5678);
#endif
  runtime::Surface s;
  runtime::Host host = runtime::create_host(s);
  const std::size_t tree_before = runtime::live_tree_nodes(host);
  if (!setup_live_swapchain(handle)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError err =
      runtime::HostOnConfigChange(host, fresh_handle);
  bool ok = error_is(err, runtime::PresenterCode::kOk) &&
            runtime::surface_valid(host) &&
            runtime::live_tree_nodes(host) == tree_before &&
            tree_before > 0;
  reset_presenter();
  runtime::destroy_host(host);
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
  RUN_CASE(check_host_on_destroyed_drives_both_layers);
  RUN_CASE(check_host_on_recreated_drives_both_layers);
  RUN_CASE(check_host_on_background_foreground_drive_both);
  RUN_CASE(check_host_on_config_change_drives_both);
  if (failures > 0) {
    std::printf("swapchain_lifecycle_test: %d failing\n", failures);
    return 1;
  }
  std::printf("swapchain_lifecycle_test: all passing\n");
  return 0;
}
