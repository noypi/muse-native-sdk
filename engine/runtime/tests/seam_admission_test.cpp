// T1 seam-gate pins — bug-runtime-013 Batch 1 (S1.2 admission).
//
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + the `runtime/egl_blit.h` public header only; opaque
// window handles are test-local fakes (never dereferenced); no NDK/EGL
// calls on host, no network, no env/config reads, no filesystem, no live
// services.
//
// Lane selection is by the canonical macro (AC-RUNTIME-041, spec-verified,
// not invented): compiled WITHOUT `-DMUSE_TEST_SEAM=1` exercises the OFF
// lane; compiled WITH `-DMUSE_TEST_SEAM=1` exercises the ON lane. The ON
// block is preprocessor-guarded so the OFF TU never names the seam (it must
// keep compiling after T2 hides the declarations).
//
// Expected red (OFF lane, current ungated tree): the fail-closed pins fail
// because the unseamed host stubs answer `true` from default hook state,
// so `EglBlitInit` succeeds where the OFF lane must fail closed.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/egl_blit.h"

#if defined(MUSE_TEST_SEAM) && (MUSE_TEST_SEAM == 1)
#define SEAM_ON 1
#else
#define SEAM_ON 0
#endif

namespace {

struct FakeNativeWindow {
  unsigned magic = 0xB117u;
};

std::vector<uint32_t> two_by_two() {
  return {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
}

// --- OFF lane: default-OFF + fail-closed (AC-RUNTIME-040 limbs a/c,
// --- AC-RUNTIME-041 single canonical macro) --------------------------------
// Compiled only when the seam macro is OFF (or undefined); never names the
// seam, so this block keeps compiling after T2 hides the declarations.
#if !SEAM_ON

// Limb (a) + AC-041: the OFF lane is the default — this TU (no -D flag)
// must not see the seam macro ON.
static bool check_off_lane_macro_not_on() { return SEAM_ON == 0; }

// Limbs (c)/(d): OFF lane carries no hook-writable state reachable from
// prod paths, so no stub may answer `true` — init on a live-looking window
// must fail closed (the frame falls back to the CPU push per support.md).
static bool check_off_init_fails_closed() {
  FakeNativeWindow w;
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitInit(static_cast<void*>(&w), &err);
  EglBlitShutdown();
  return !ok;
}

// Same limb, full-frame shape: init reports failure AND names a trigger
// (fail-closed with a diagnostic, never silent, never blank).
static bool check_off_init_failure_carries_trigger() {
  FakeNativeWindow w;
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitInit(static_cast<void*>(&w), &err);
  EglBlitShutdown();
  return !ok && err.code != BlitCode::kOk && !err.trigger.empty();
}

// Baseline (green before and after): present without a live presenter
// already fails closed.
static bool check_off_present_without_init_fails_closed() {
  EglBlitShutdown();
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitPresent(px.data(), 2, 2, &err);
  return !ok && err.code != BlitCode::kOk;
}

// Baseline: shutdown stays idempotent on the OFF lane (no seam teardown).
static bool check_off_shutdown_idempotent() {
  EglBlitShutdown();
  EglBlitShutdown();
  return true;
}

#endif  // !SEAM_ON

#if SEAM_ON
// --- ON lane: seam exposed for the two migrating suites --------------------
// (engine/runtime/tests/egl_blit_test.cpp, present_fallback_test.cpp).
// Compiled only under -DMUSE_TEST_SEAM=1; skipped entirely on OFF builds.
static bool check_on_seam_injects_swap_failure() {
  FakeNativeWindow w;
  egl_blit_test::FaultHooks hooks;
  hooks.fail_next_swap = true;
  egl_blit_test::SetFaultHooks(&hooks);
  BlitError init_err{BlitCode::kOk, ""};
  bool init_ok = EglBlitInit(static_cast<void*>(&w), &init_err);
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool presented = false;
  if (init_ok) presented = EglBlitPresent(px.data(), 2, 2, &err);
  egl_blit_test::SetFaultHooks(nullptr);
  EglBlitShutdown();
  return init_ok && !presented && err.code == BlitCode::kSwapFailed &&
         !err.trigger.empty();
}

static bool check_on_seam_clear_restores_ok() {
  FakeNativeWindow w;
  egl_blit_test::SetFaultHooks(nullptr);
  BlitError init_err{BlitCode::kOk, ""};
  bool init_ok = EglBlitInit(static_cast<void*>(&w), &init_err);
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool presented = false;
  if (init_ok) presented = EglBlitPresent(px.data(), 2, 2, &err);
  EglBlitShutdown();
  return init_ok && presented && err.code == BlitCode::kOk;
}
#endif  // SEAM_ON

}  // namespace

#define RUN_CASE(name)                                    \
  do {                                                    \
    if (!(name)()) {                                      \
      std::printf("FAIL %s\n", #name);                    \
      ++failures;                                         \
    } else {                                              \
      std::printf("PASS %s\n", #name);                    \
    }                                                     \
  } while (0)

int main() {
  int failures = 0;
#if SEAM_ON
  RUN_CASE(check_on_seam_injects_swap_failure);
  RUN_CASE(check_on_seam_clear_restores_ok);
#else
  RUN_CASE(check_off_lane_macro_not_on);
  RUN_CASE(check_off_init_fails_closed);
  RUN_CASE(check_off_init_failure_carries_trigger);
  RUN_CASE(check_off_present_without_init_fails_closed);
  RUN_CASE(check_off_shutdown_idempotent);
#endif
  if (failures > 0) {
    std::printf("seam_admission_test: %d failing\n", failures);
    return 1;
  }
  std::printf("seam_admission_test: all passing\n");
  return 0;
}
