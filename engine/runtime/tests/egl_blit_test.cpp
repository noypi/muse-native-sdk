// Batch 2 contract tests — FR-GRAPHICS-012 / AC-GRAPHICS-033.
// Binds ONLY to the T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `runtime/egl_blit.h` public header only; opaque
// window handles are test-local fakes (never dereferenced); no NDK/EGL
// calls on host, no network, no env/config reads, no filesystem, no live
// services. The host EGL underneath is the coder's stub-EGL (their
// production claim); fault injection below goes through the test-only
// `egl_blit_test::SetFaultHooks` seam the coder provides on host
// (see ASSUMPTION A1 in unit-tests/T2.md).
//
// Expected red: this TU does not compile until the coder publishes
// `engine/runtime/include/runtime/egl_blit.h` (+ sources + CMake
// registration) — `runtime/egl_blit.h: No such file or directory`.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/egl_blit.h"

// --- test-only fault-injection seam ------------------------------------------
// Provided by `runtime/egl_blit.h` (coder's host stub-EGL claim; device lane
// may no-op it). `nullptr` clears all faults. Minimal surface for the plan's
// "stub EGL (swap-fail injection -> kSwapFailed)" requirement: exactly the
// faults the public API cannot otherwise induce hermetically.
//   namespace egl_blit_test {
//   struct FaultHooks {
//     bool fail_init = false;       // EGL init path reports failure
//     bool lose_context = false;    // context reports lost
//     bool fail_next_swap = false;  // next eglSwapBuffers reports failure
//     bool no_surface = false;      // backgrounded/destroyed surface
//   };
//   void SetFaultHooks(const FaultHooks* hooks);
//   }

namespace {

struct FaultGuard {
  FaultGuard() { egl_blit_test::SetFaultHooks(nullptr); }
  ~FaultGuard() {
    egl_blit_test::SetFaultHooks(nullptr);
    EglBlitShutdown();
  }
};

struct FakeNativeWindow {
  unsigned magic = 0xB117u;
};

bool trigger_names_call(const BlitError& err, const char* needle) {
  if (err.trigger.empty() || needle == nullptr) return false;
  // Case-insensitive substring: trigger must NAME the failing EGL call.
  std::string hay = err.trigger;
  std::string ndl = needle;
  for (char& c : hay) c = static_cast<char>(std::tolower(c));
  for (char& c : ndl) c = static_cast<char>(std::tolower(c));
  return hay.find(ndl) != std::string::npos;
}

bool has_trigger(const BlitError& err) { return !err.trigger.empty(); }

std::vector<uint32_t> two_by_two() {
  return {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
}

}  // namespace

// --- harness cases (convention copied from present_frame_test.cpp) -----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T2 cases ----------------------------------------------------------------

// Named case: init with a null window fails closed with kNoSurface and a
// trigger naming the failing call. (ASSUMPTION A2: null window -> kNoSurface.)
static bool check_init_null_window_fails_closed() {
  FaultGuard g;
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitInit(nullptr, &err);
  return !ok && err.code == BlitCode::kNoSurface && has_trigger(err) &&
         trigger_names_call(err, "surface");
}

// Named case: error == nullptr is tolerated on init (no crash); the boolean
// still reports the outcome.
static bool check_init_tolerates_null_error() {
  FaultGuard g;
  FakeNativeWindow w;
  bool ok_valid = EglBlitInit(static_cast<void*>(&w), nullptr);
  EglBlitShutdown();
  bool ok_null = EglBlitInit(nullptr, nullptr);
  (void)ok_valid;
  (void)ok_null;
  return true;  // reaching here without crashing is the assertion
}

// Named case: present with no prior init fails closed (never presents);
// trigger names the failure. (ASSUMPTION A3: exact code is the coder's
// claim; kNoSurface expected — assert non-kOk + trigger, exact code below
// is checked as kNoSurface per A3.)
static bool check_present_without_init_fails_closed() {
  FaultGuard g;
  BlitError err{BlitCode::kOk, ""};
  std::vector<uint32_t> px = two_by_two();
  bool ok = EglBlitPresent(px.data(), 2, 2, &err);
  return !ok && err.code == BlitCode::kNoSurface && has_trigger(err);
}

// Named case: error == nullptr is tolerated on present (no crash), including
// on the failure path.
static bool check_present_tolerates_null_error() {
  FaultGuard g;
  std::vector<uint32_t> px = two_by_two();
  bool ok = EglBlitPresent(px.data(), 2, 2, nullptr);
  (void)ok;
  return true;  // reaching here without crashing is the assertion
}

// Named case: null pixels never present — fail closed with a named trigger.
static bool check_present_null_pixels_fails_closed() {
  FaultGuard g;
  FakeNativeWindow w;
  BlitError init_err{BlitCode::kOk, ""};
  if (!EglBlitInit(static_cast<void*>(&w), &init_err)) return false;
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitPresent(nullptr, 2, 2, &err);
  return !ok && err.code != BlitCode::kOk && has_trigger(err);
}

// Named case: zero/negative dimensions never present.
static bool check_present_zero_dims_fails_closed() {
  FaultGuard g;
  FakeNativeWindow w;
  BlitError init_err{BlitCode::kOk, ""};
  if (!EglBlitInit(static_cast<void*>(&w), &init_err)) return false;
  std::vector<uint32_t> px = two_by_two();
  BlitError e1{BlitCode::kOk, ""}, e2{BlitCode::kOk, ""},
      e3{BlitCode::kOk, ""};
  bool ok = false;
  ok = EglBlitPresent(px.data(), 0, 2, &e1) || ok;
  ok = EglBlitPresent(px.data(), 2, 0, &e2) || ok;
  ok = EglBlitPresent(px.data(), -1, 2, &e3) || ok;
  return !ok && e1.code != BlitCode::kOk && e2.code != BlitCode::kOk &&
         e3.code != BlitCode::kOk && has_trigger(e1) && has_trigger(e2) &&
         has_trigger(e3);
}

// Named case: one presenter per ANativeWindow — a second init on the same
// live window fails closed (EGL and Vulkan never co-present).
// (ASSUMPTION A4: second init returns false; exact code is coder's claim.)
static bool check_double_init_same_window_fails() {
  FaultGuard g;
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  BlitError e1{BlitCode::kOk, ""}, e2{BlitCode::kOk, ""};
  if (!EglBlitInit(handle, &e1)) return false;
  bool second = EglBlitInit(handle, &e2);
  return !second && e2.code != BlitCode::kOk && has_trigger(e2);
}

// Named case: stub-EGL swap-fail injection maps to kSwapFailed with the
// trigger naming eglSwapBuffers (plan Detail, exact check).
static bool check_swap_fail_injects_kSwapFailed() {
  FaultGuard g;
  FakeNativeWindow w;
  BlitError init_err{BlitCode::kOk, ""};
  if (!EglBlitInit(static_cast<void*>(&w), &init_err)) return false;
  egl_blit_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitPresent(px.data(), 2, 2, &err);
  return !ok && err.code == BlitCode::kSwapFailed &&
         trigger_names_call(err, "swap");
}

// Named case: stub-EGL context-loss injection maps to kContextLost with a
// trigger naming the context call (eglMakeCurrent).
static bool check_context_loss_injects_kContextLost() {
  FaultGuard g;
  FakeNativeWindow w;
  BlitError init_err{BlitCode::kOk, ""};
  if (!EglBlitInit(static_cast<void*>(&w), &init_err)) return false;
  egl_blit_test::FaultHooks hooks;
  hooks.lose_context = true;
  egl_blit_test::SetFaultHooks(&hooks);
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitPresent(px.data(), 2, 2, &err);
  return !ok && err.code == BlitCode::kContextLost && has_trigger(err) &&
         (trigger_names_call(err, "context") ||
          trigger_names_call(err, "current") ||
          trigger_names_call(err, "lost"));
}

// Named case: after shutdown the presenter is gone — present fails closed
// (kNoSurface expected; assert non-kOk + trigger) and shutdown stays
// idempotent; re-init on the window succeeds (no wedged state).
static bool check_shutdown_then_present_fails_and_reinit_ok() {
  FaultGuard g;
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
  BlitError init_err{BlitCode::kOk, ""};
  if (!EglBlitInit(handle, &init_err)) return false;
  EglBlitShutdown();
  EglBlitShutdown();  // idempotent — must not crash
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool presented = EglBlitPresent(px.data(), 2, 2, &err);
  if (presented || err.code == BlitCode::kOk || !has_trigger(err))
    return false;
  BlitError re_err{BlitCode::kOk, ""};
  return EglBlitInit(handle, &re_err) && re_err.code == BlitCode::kOk;
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
  RUN_CASE(check_init_null_window_fails_closed);
  RUN_CASE(check_init_tolerates_null_error);
  RUN_CASE(check_present_without_init_fails_closed);
  RUN_CASE(check_present_tolerates_null_error);
  RUN_CASE(check_present_null_pixels_fails_closed);
  RUN_CASE(check_present_zero_dims_fails_closed);
  RUN_CASE(check_double_init_same_window_fails);
  RUN_CASE(check_swap_fail_injects_kSwapFailed);
  RUN_CASE(check_context_loss_injects_kContextLost);
  RUN_CASE(check_shutdown_then_present_fails_and_reinit_ok);
  if (failures > 0) {
    std::printf("egl_blit_test: %d failing\n", failures);
    return 1;
  }
  std::printf("egl_blit_test: all passing\n");
  return 0;
}
