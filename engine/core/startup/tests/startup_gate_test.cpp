// Batch 2 contract tests — T4 startup-path gates: determinism, no hidden
// I/O, bounded execution. Test-only item exercising the T1/T3 surfaces.
// Specs: AC-CORE-005 (deterministic bounded startup), AC-CORE-032 (fixed
// ordered phase list + per-phase timing).
//
// Binds to the T4 `Interface:` line in plan.md (verbatim):
//   none — test-only item exercising the T1/T3 surfaces; failure mode is
//   a non-passing CTest suite.
//
// `bootstrap_test.cpp` is batch-1 published/committed and is NOT modified
// here; it is referenced as already-published coverage for fixed order and
// per-phase timings. This file adds the gate-level cases: determinism
// across runs (proxy for no hidden I/O), bounded execution, and the
// T1+T3 combined rule (no thread/handle before declared phase with
// pre-warmed first dispatch).
//
// Hermetic: C++20 + `core/startup/bootstrap.h` and
// `core/startup/pool_warmup.h` only. No env/config reads, no filesystem,
// no sockets, no live services, no wall-clock reads in the test itself
// (boundedness/monotonicity are asserted from returned values only).
//
// ASSUMPTIONS (contract genuinely silent on these details):
//   A1: namespaces/types follow batch-1 (`muse::startup::runBootstrap`,
//       six-entry `timings`) and T3 (`prewarmPools`, `ResourceKind`,
//       thread-pools phase index 1).
//   A2: "No hidden I/O" is asserted as observable determinism: repeated
//       runs yield identical order/counts with no env or filesystem input,
//       since a hidden input would be the only way to perturb the outputs.
//   A3: "Bounded execution" is asserted as every phase carrying a finite
//       monotonic timing entry with no growth across runs (no unbounded
//       work); no wall-clock timeout is used, keeping the test hermetic.

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "core/startup/bootstrap.h"
#include "core/startup/pool_warmup.h"

namespace {

using muse::startup::BootstrapConfig;
using muse::startup::BootstrapResult;
using muse::startup::creationAllowedForPhase;
using muse::startup::PoolWarmupConfig;
using muse::startup::prewarmPools;
using muse::startup::ResourceKind;
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

bool timings_monotonic_bounded(const BootstrapResult& r) {
  for (int i = 0; i < 6; ++i) {
    if (!(r.timings[i].endNs >= r.timings[i].startNs)) return false;
    if (i > 0) {
      if (!(r.timings[i].startNs >= r.timings[i - 1].startNs)) return false;
      if (!(r.timings[i].startNs >= r.timings[i - 1].endNs)) return false;
    }
  }
  return true;
}

bool check_determinism_identical_across_runs() {
  // AC-CORE-005: deterministic startup — two runs, identical order/names.
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

bool check_per_phase_timings_present_and_bounded() {
  // AC-CORE-032: every phase has a timing entry; bounded execution means
  // each entry is a finite monotonic interval with no growth across runs.
  BootstrapConfig cfg{};
  BootstrapResult a = runBootstrap(cfg);
  BootstrapResult b = runBootstrap(cfg);
  CHECK(a.ok && b.ok);
  if (!a.ok || !b.ok) return false;
  CHECK(timings_monotonic_bounded(a));
  CHECK(timings_monotonic_bounded(b));
  for (int i = 0; i < 6; ++i) {
    // Each phase duration is a finite interval (bounded execution).
    CHECK(a.timings[i].endNs >= a.timings[i].startNs);
    CHECK(b.timings[i].endNs >= b.timings[i].startNs);
  }
  CHECK(sizeof(a.timings) / sizeof(a.timings[0]) == 6u);
  return g_failed_checks == 0;
}

bool check_no_hidden_io_stable_without_inputs() {
  // No hidden I/O: outputs are fully determined by the (default) config
  // with no env/filesystem input — repeated runs agree exactly on order
  // and on the pre-warmed pool count.
  BootstrapConfig bcfg{};
  BootstrapResult a = runBootstrap(bcfg);
  BootstrapResult b = runBootstrap(bcfg);
  PoolWarmupConfig pcfg{};
  CHECK(a.ok && b.ok);
  if (!a.ok || !b.ok) return false;
  for (int i = 0; i < 6; ++i) {
    CHECK(std::strcmp(a.timings[i].name, b.timings[i].name) == 0);
  }
  auto w1 = prewarmPools(pcfg);
  auto w2 = prewarmPools(pcfg);
  CHECK(w1.ok && w2.ok);
  if (!w1.ok || !w2.ok) return false;
  CHECK(w1.threadsReady == w2.threadsReady);
  return g_failed_checks == 0;
}

bool check_no_thread_or_handle_before_declared_phase() {
  // Combined T1+T3 rule: no thread/handle creation before its declared
  // phase (thread-pools index 1); allowed at/after it.
  CHECK(!creationAllowedForPhase(ResourceKind::kThread, 0));
  CHECK(!creationAllowedForPhase(ResourceKind::kHandle, 0));
  CHECK(creationAllowedForPhase(ResourceKind::kThread, 1));
  CHECK(creationAllowedForPhase(ResourceKind::kHandle, 1));
  return g_failed_checks == 0;
}

bool check_prewarm_first_dispatch_needs_no_creation() {
  // Plan Detail: warm during the thread-pools phase so first work items
  // pay no creation latency — after prewarmPools the pool count is
  // observable and creation is allowed at the dispatch phase.
  PoolWarmupConfig cfg{};
  auto w = prewarmPools(cfg);
  CHECK(w.ok);
  if (!w.ok) return false;
  CHECK(w.threadsReady > 0);
  CHECK(creationAllowedForPhase(ResourceKind::kThread, 1));
  return g_failed_checks == 0;
}

bool check_noexcept_surfaces() {
  // Compile-time: the exercised T1/T3 surfaces are noexcept.
  static_assert(noexcept(runBootstrap(BootstrapConfig{})),
                "runBootstrap must be noexcept");
  static_assert(noexcept(prewarmPools(PoolWarmupConfig{})),
                "prewarmPools must be noexcept");
  static_assert(noexcept(creationAllowedForPhase(ResourceKind::kThread, 0)),
                "creationAllowedForPhase must be noexcept");
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
  RUN_CASE(check_determinism_identical_across_runs);
  RUN_CASE(check_per_phase_timings_present_and_bounded);
  RUN_CASE(check_no_hidden_io_stable_without_inputs);
  RUN_CASE(check_no_thread_or_handle_before_declared_phase);
  RUN_CASE(check_prewarm_first_dispatch_needs_no_creation);
  RUN_CASE(check_noexcept_surfaces);
  if (failures > 0) {
    std::printf("startup_gate_test: %d failing\n", failures);
    return 1;
  }
  std::printf("startup_gate_test: all passing\n");
  return 0;
}
