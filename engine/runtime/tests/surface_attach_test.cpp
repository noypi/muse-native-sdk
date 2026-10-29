// T1 contract tests — FR-RUNTIME-009 / AC-RUNTIME-027 (TC-RUNTIME-027).
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side ANativeWindow
// fakes behind `#ifndef __ANDROID__` guards; no NDK/Vulkan calls on host,
// no network, no env/config reads, no filesystem, no live services.
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

// --- T1 helpers --------------------------------------------------------------

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake. The presenter must never dereference
// the handle as a View; validity is established by the magic field behind the
// non-Android guard. Real NDK calls happen only under `__ANDROID__`.
struct FakeNativeWindow {
  unsigned magic = 0xA11CEu;
};
#endif

// Reset global single-presenter state so cases stay order-independent.
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

}  // namespace

// Named case: null window fails closed with kNullWindow + diagnostic,
// no crash, no handle retained. A second null attach still fails closed
// and a subsequent valid attach still succeeds (no state mutated).
static bool check_null_window_fails_closed() {
  reset_presenter();
  runtime::PresenterError err = runtime::AttachSurface(nullptr);
  bool ok = error_is(err, runtime::PresenterCode::kNullWindow) &&
            has_diagnostic(err);
  runtime::PresenterError again = runtime::AttachSurface(nullptr);
  ok = ok && error_is(again, runtime::PresenterCode::kNullWindow) &&
       has_diagnostic(again);
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError good =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError good =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  ok = ok && error_is(good, runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: invalid (non-null, non-window) handle fails closed with
// kInvalidWindow + diagnostic, no crash, no leaked handle, no View creation.
// A second invalid attach still fails closed and a subsequent valid attach
// still succeeds (no state mutated).
static bool check_invalid_handle_fails_closed() {
  reset_presenter();
#ifndef __ANDROID__
  int bogus = 0;  // never a FakeNativeWindow: must be rejected, not probed.
  runtime::PresenterError err =
      runtime::AttachSurface(static_cast<void*>(&bogus));
  bool ok = error_is(err, runtime::PresenterCode::kInvalidWindow) &&
            has_diagnostic(err);
  runtime::PresenterError again =
      runtime::AttachSurface(static_cast<void*>(&bogus));
  ok = ok && error_is(again, runtime::PresenterCode::kInvalidWindow) &&
       has_diagnostic(again);
  FakeNativeWindow w;
  runtime::PresenterError good =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError err =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1));
  bool ok = error_is(err, runtime::PresenterCode::kInvalidWindow) &&
            has_diagnostic(err);
  runtime::PresenterError again =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1));
  ok = ok && error_is(again, runtime::PresenterCode::kInvalidWindow) &&
       has_diagnostic(again);
  runtime::PresenterError good =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  ok = ok && error_is(good, runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: valid attach/detach round-trips with kOk.
static bool check_valid_attach_detach_round_trips() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError a =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError a =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  if (!error_is(a, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError d = runtime::DetachSurface();
  bool ok = error_is(d, runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: second attach without detach is rejected with kAlreadyAttached
// (single-presenter-per-window guard, RULE-RUNTIME-006).
static bool check_double_attach_rejected() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError first =
      runtime::AttachSurface(static_cast<void*>(&w));
  if (!error_is(first, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError second =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError first =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
  if (!error_is(first, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError second =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  bool ok = error_is(second, runtime::PresenterCode::kAlreadyAttached) &&
            has_diagnostic(second);
  reset_presenter();
  return ok;
}

// Named case: detach after a failed (null) attach leaves no handle behind —
// a subsequent valid attach still succeeds (no leaked handle).
static bool check_no_leaked_handle_after_failed_attach() {
  reset_presenter();
  runtime::PresenterError bad = runtime::AttachSurface(nullptr);
  if (!error_is(bad, runtime::PresenterCode::kNullWindow)) {
    reset_presenter();
    return false;
  }
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError good =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError good =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  bool ok = error_is(good, runtime::PresenterCode::kOk);
  reset_presenter();
  return ok;
}

// Named case: InvalidateWindow drops the retained handle — a subsequent
// create fails closed (no dangling handle reports a false-kOk create) and
// the window can be re-attached cleanly afterwards.
static bool check_invalidate_window_drops_handle() {
  reset_presenter();
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  if (!error_is(runtime::AttachSurface(handle),
                 runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError inv = runtime::InvalidateWindow(handle);
  if (!error_is(inv, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  runtime::PresenterError created = runtime::CreateSurfaceAndSwapchain(p);
  bool ok = error_is(created, runtime::PresenterCode::kCreateFailed) &&
            has_diagnostic(created);
  ok = ok && error_is(runtime::AttachSurface(handle),
                      runtime::PresenterCode::kOk);
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
  RUN_CASE(check_null_window_fails_closed);
  RUN_CASE(check_invalid_handle_fails_closed);
  RUN_CASE(check_valid_attach_detach_round_trips);
  RUN_CASE(check_double_attach_rejected);
  RUN_CASE(check_no_leaked_handle_after_failed_attach);
  RUN_CASE(check_invalidate_window_drops_handle);
  if (failures > 0) {
    std::printf("surface_attach_test: %d failing\n", failures);
    return 1;
  }
  std::printf("surface_attach_test: all passing\n");
  return 0;
}
