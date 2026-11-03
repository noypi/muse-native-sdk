// Security pin (2) — OFF fail-closed probe (bug-runtime-013, security review 2026-11-03).
//
// AC-RUNTIME-040 limb c: OFF-lane prod paths carry no hook-writable state, so
// every EGL failure degrades to the fail-closed stub behavior (init/present
// report failure with a trigger; the frame falls back to the CPU push).
//
// Hermetic: C++17 + `runtime/egl_blit.h` public boundary only; opaque window
// handles are test-local fakes (never dereferenced); no NDK/EGL calls, no
// network, no env/config reads, no filesystem, no live services. Fault shapes
// below MIRROR each `FaultHooks` flag (fail_init / lose_context /
// fail_next_swap / no_surface) using only prod-reachable inputs — this TU
// never names `egl_blit_test::` anything, so it keeps compiling after (and
// because) T2 hides the seam. Must be compiled WITHOUT `-DMUSE_TEST_SEAM=1`.

#if defined(MUSE_TEST_SEAM) && (MUSE_TEST_SEAM == 1)
#error "seam_off_failclosed_test: OFF-lane pin — compile WITHOUT -DMUSE_TEST_SEAM=1"
#endif

#include <cstdint>
#include <cstdio>
#include <vector>

#include "runtime/egl_blit.h"

namespace {

struct FakeNativeWindow {
  unsigned magic = 0xB117u;
};

std::vector<uint32_t> two_by_two() {
  return {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
}

// Mirrors fail_init/no_surface: even a live-looking window cannot init on the
// OFF host lane (HostEglCreateSurface answers false) — init fails closed.
static bool check_off_faultshaped_init_fails_closed() {
  FakeNativeWindow w;
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitInit(static_cast<void*>(&w), &err);
  EglBlitShutdown();
  return !ok && err.code != BlitCode::kOk && !err.trigger.empty();
}

// Mirrors no_surface via the null-window prod path (caller-supplied opaque
// pointer, null-checked only): still fail-closed, never a crash.
static bool check_off_null_window_fails_closed() {
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitInit(nullptr, &err);
  EglBlitShutdown();
  return !ok && err.code != BlitCode::kOk && !err.trigger.empty();
}

// Mirrors lose_context/fail_next_swap via present-without-init: with no live
// presenter the context/swap path cannot answer `true`.
static bool check_off_present_without_init_fails_closed() {
  EglBlitShutdown();
  std::vector<uint32_t> px = two_by_two();
  BlitError err{BlitCode::kOk, ""};
  bool ok = EglBlitPresent(px.data(), 2, 2, &err);
  return !ok && err.code != BlitCode::kOk && !err.trigger.empty();
}

// Null error sink must still fail closed (no crash, no success without state).
static bool check_off_null_error_ptr_fails_closed() {
  FakeNativeWindow w;
  bool ok = EglBlitInit(static_cast<void*>(&w), nullptr);
  EglBlitShutdown();
  return !ok;
}

// Shutdown stays idempotent under repeated fault-shaped cycles.
static bool check_off_repeated_cycle_stays_fail_closed() {
  FakeNativeWindow w;
  for (int i = 0; i < 2; ++i) {
    BlitError err{BlitCode::kOk, ""};
    if (EglBlitInit(static_cast<void*>(&w), &err)) {
      EglBlitShutdown();
      return false;
    }
    EglBlitShutdown();
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                                \
  do {                                                \
    if (!(name)()) {                                  \
      std::printf("FAIL %s\n", #name);                \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", #name);                \
    }                                                 \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_off_faultshaped_init_fails_closed);
  RUN_CASE(check_off_null_window_fails_closed);
  RUN_CASE(check_off_present_without_init_fails_closed);
  RUN_CASE(check_off_null_error_ptr_fails_closed);
  RUN_CASE(check_off_repeated_cycle_stays_fail_closed);
  if (failures > 0) {
    std::printf("seam_off_failclosed_test: %d failing\n", failures);
    return 1;
  }
  std::printf("seam_off_failclosed_test: all passing\n");
  return 0;
}
