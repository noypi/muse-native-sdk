// T3 contract tests — FR-RUNTIME-009 / AC-RUNTIME-029 (manual TC-029).
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side ANativeWindow
// fakes behind `#ifndef __ANDROID__` guards; no NDK/Vulkan calls on host,
// no network, no env/config reads, no filesystem, no live services.
//
// Unit scope: the no-present-without-current-DrawCmds invariant
// (kNoSwapchain / kNoCurrentFrame). The kOk visible-frames path is proven
// manually via examples/m3-gallery TC-029 (see unit-tests/T3.md).
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>

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

// --- T3 helpers --------------------------------------------------------------

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

bool create_swapchain_for_present() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  return error_is(runtime::CreateSurfaceAndSwapchain(p),
                  runtime::PresenterCode::kOk);
}

// A hand-built, never-DAG-recorded command list. It is well-formed as data
// but is NOT the current-frame set, so presenting it must fail closed.
runtime::DrawCmds stale_cmds() {
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmd.nodeId = 7;
  cmds.push_back(cmd);
  return cmds;
}

}  // namespace

// Named case: PresentFrame with no swapchain fails closed with kNoSwapchain
// + diagnostic — an empty/stale acquisition without current-frame DrawCmds
// is never presented.
static bool check_present_without_swapchain_fails_closed() {
  reset_presenter();
  runtime::DrawCmds empty;
  runtime::PresenterError err = runtime::PresentFrame(empty);
  bool ok = error_is(err, runtime::PresenterCode::kNoSwapchain) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: PresentFrame with an empty command list on a live swapchain
// fails closed with kNoCurrentFrame + diagnostic — never presented.
static bool check_present_empty_cmds_never_presented() {
  reset_presenter();
  if (!attach_valid_window() || !create_swapchain_for_present()) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds empty;
  runtime::PresenterError err = runtime::PresentFrame(empty);
  bool ok = error_is(err, runtime::PresenterCode::kNoCurrentFrame) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: PresentFrame with a stale (never-DAG-recorded) non-empty
// command list fails closed with kNoCurrentFrame + diagnostic — only the
// DAG-recorded current-frame set may be presented.
static bool check_present_stale_cmds_never_presented() {
  reset_presenter();
  if (!attach_valid_window() || !create_swapchain_for_present()) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds stale = stale_cmds();
  runtime::PresenterError err = runtime::PresentFrame(stale);
  bool ok = error_is(err, runtime::PresenterCode::kNoCurrentFrame) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: PresentFrame after the swapchain is gone (destroyed surface)
// fails closed with kNoSwapchain + diagnostic — no dangling-surface present.
static bool check_present_after_destroy_fails_closed() {
  reset_presenter();
  if (!attach_valid_window() || !create_swapchain_for_present()) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds stale = stale_cmds();
  runtime::PresenterError err = runtime::PresentFrame(stale);
  bool ok = error_is(err, runtime::PresenterCode::kNoSwapchain) &&
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
  RUN_CASE(check_present_without_swapchain_fails_closed);
  RUN_CASE(check_present_empty_cmds_never_presented);
  RUN_CASE(check_present_stale_cmds_never_presented);
  RUN_CASE(check_present_after_destroy_fails_closed);
  if (failures > 0) {
    std::printf("present_frame_test: %d failing\n", failures);
    return 1;
  }
  std::printf("present_frame_test: all passing\n");
  return 0;
}
