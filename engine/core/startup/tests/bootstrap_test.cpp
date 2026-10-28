// Batch 1 contract tests — T1 numbered bootstrap phase list with per-phase timing.
// Specs: AC-CORE-032 (fixed ordered phase list + per-phase timing to telemetry),
//         AC-CORE-005 (deterministic bounded startup).
//
// Binds to the T1 `Interface:` line in plan.md (verbatim):
//   `BootstrapResult runBootstrap(const BootstrapConfig& config) noexcept;`
//   returning `struct BootstrapResult { bool ok; PhaseTiming timings[6];
//   StartupError error; };` where failure returns `ok=false` plus a named
//   `StartupError{ code, phase, message }` and no partial execution beyond
//   the failed phase.
//
// Hermetic: C++20 + `core/startup/bootstrap.h` only. No env/config reads, no
// filesystem, no sockets, no live services, no wall-clock reads in the test
// itself (monotonicity is asserted from the returned PhaseTiming values).
//
// ASSUMPTIONS (contract genuinely silent on these details):
//   A1: namespace is `muse::startup` (mirrors `muse::bridge` convention).
//   A2: `BootstrapConfig` has a default-constructible `allowEager` flag
//       defaulting to false (plan: "eager-only work behind explicit
//       `allowEager` gate").
//   A3: `PhaseTiming` exposes `{ phase, name, startNs, endNs }` with a
//       monotonic tick domain; `StartupError` exposes `{ code, phase,
//       message }` with non-empty message on failure.
//   A4: Fixed order is memory/allocator → thread pools → GPU device →
//       JsRuntime → UI tree → first frame (plan Detail / CORE-012 notes).

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "core/startup/bootstrap.h"

namespace {

using muse::startup::BootstrapConfig;
using muse::startup::BootstrapResult;
using muse::startup::runBootstrap;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

const char* kExpectedOrder[6] = {
    "memory/allocator",
    "thread pools",
    "GPU device",
    "JsRuntime",
    "UI tree",
    "first frame",
};

bool timings_monotonic(const BootstrapResult& r) {
  for (int i = 0; i < 6; ++i) {
    if (!(r.timings[i].startNs <= r.timings[i].endNs)) return false;
    if (i > 0) {
      if (!(r.timings[i].startNs >= r.timings[i - 1].startNs)) return false;
      if (!(r.timings[i].startNs >= r.timings[i - 1].endNs)) return false;
    }
  }
  return true;
}

bool check_fixed_order_six_phases() {
  // AC-CORE-032: fixed ordered phase list, six entries, exact names/order.
  BootstrapConfig cfg{};
  BootstrapResult r = runBootstrap(cfg);
  CHECK(r.ok);
  if (!r.ok) return false;
  for (int i = 0; i < 6; ++i) {
    CHECK(r.timings[i].phase == i);
    CHECK(r.timings[i].name != nullptr);
    CHECK(std::strcmp(r.timings[i].name, kExpectedOrder[i]) == 0);
  }
  return g_failed_checks == 0;
}

bool check_twice_identical_order() {
  // Plan acceptance: running bootstrap twice yields identical phase order.
  BootstrapConfig cfg{};
  BootstrapResult a = runBootstrap(cfg);
  BootstrapResult b = runBootstrap(cfg);
  CHECK(a.ok && b.ok);
  if (!a.ok || !b.ok) return false;
  for (int i = 0; i < 6; ++i) {
    CHECK(a.timings[i].phase == b.timings[i].phase);
    CHECK(std::strcmp(a.timings[i].name, b.timings[i].name) == 0);
  }
  return g_failed_checks == 0;
}

bool check_every_phase_has_timing() {
  // AC-CORE-032: every phase records monotonic start/end exposed to telemetry.
  BootstrapConfig cfg{};
  BootstrapResult r = runBootstrap(cfg);
  CHECK(r.ok);
  if (!r.ok) return false;
  CHECK(timings_monotonic(r));
  for (int i = 0; i < 6; ++i) {
    // Timing entry present: end strictly after start or at least ordered.
    CHECK(r.timings[i].endNs >= r.timings[i].startNs);
  }
  return g_failed_checks == 0;
}

bool check_eager_gate_defaults_off() {
  // Plan Detail: eager-only work behind explicit `allowEager` gate.
  BootstrapConfig cfg{};
  CHECK(cfg.allowEager == false);
  BootstrapResult off = runBootstrap(cfg);
  CHECK(off.ok);
  cfg.allowEager = true;
  BootstrapResult on = runBootstrap(cfg);
  CHECK(on.ok);
  if (!off.ok || !on.ok) return false;
  // Deterministic regardless of the gate: same phase order either way.
  for (int i = 0; i < 6; ++i) {
    CHECK(std::strcmp(off.timings[i].name, on.timings[i].name) == 0);
  }
  return g_failed_checks == 0;
}

bool check_failure_shape_and_no_partial_execution() {
  // Plan Interface: failure returns ok=false plus named
  // StartupError{ code, phase, message } and no partial execution beyond
  // the failed phase. Exercised for real via the diagnostic
  // `failAtPhase` injection hook (documented test-only in bootstrap.h);
  // the default config preserves infallible-by-default success.
  {
    BootstrapConfig cfg{};
    CHECK(cfg.failAtPhase == -1);
    BootstrapResult r = runBootstrap(cfg);
    CHECK(r.ok);
    CHECK(r.error.code == 0);
    if (!r.ok) return false;
  }
  {
    BootstrapConfig cfg{};
    cfg.failAtPhase = 2;
    BootstrapResult r = runBootstrap(cfg);
    CHECK(!r.ok);
    if (r.ok) return false;
    CHECK(r.error.code != 0);
    CHECK(r.error.phase == 2);
    CHECK(r.error.message != nullptr);
    CHECK(r.error.message[0] != '\0');
    CHECK(std::strstr(r.error.message, "GPU device") != nullptr);
    // Phases before the failed one executed: stamped timings.
    for (int i = 0; i < 2; ++i) {
      CHECK(r.timings[i].phase == i);
      CHECK(r.timings[i].name != nullptr);
      CHECK(std::strcmp(r.timings[i].name, kExpectedOrder[i]) == 0);
      CHECK(r.timings[i].endNs >= r.timings[i].startNs);
      CHECK(r.timings[i].startNs != 0 || r.timings[i].endNs != 0);
    }
    // No partial execution at/beyond the failed phase: zeroed timings.
    for (int i = 2; i < 6; ++i) {
      CHECK(r.timings[i].startNs == 0 && r.timings[i].endNs == 0);
    }
  }
  return g_failed_checks == 0;
}

bool check_noexcept_and_result_width() {
  // Compile-time: noexcept signature and six-entry timing array.
  static_assert(noexcept(runBootstrap(BootstrapConfig{})),
                "runBootstrap must be noexcept");
  BootstrapConfig cfg{};
  BootstrapResult r = runBootstrap(cfg);
  CHECK(sizeof(r.timings) / sizeof(r.timings[0]) == 6u);
  (void)r;
  return g_failed_checks == 0;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    g_failed_checks = 0;               \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_fixed_order_six_phases);
  RUN_CASE(check_twice_identical_order);
  RUN_CASE(check_every_phase_has_timing);
  RUN_CASE(check_eager_gate_defaults_off);
  RUN_CASE(check_failure_shape_and_no_partial_execution);
  RUN_CASE(check_noexcept_and_result_width);
  if (failures > 0) {
    std::printf("bootstrap_test: %d failing\n", failures);
    return 1;
  }
  std::printf("bootstrap_test: all passing\n");
  return 0;
}
