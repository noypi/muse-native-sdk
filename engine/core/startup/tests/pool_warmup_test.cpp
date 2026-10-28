// Batch 2 contract tests — T3 pre-warm worker pools + creation gate.
// Specs: AC-CORE-005 (deterministic bounded startup), AC-CORE-033
// (phase gate), NFR-CORE-010 (pool internals owned by CORE-010; startup
// calls only create/warm entry points).
//
// Binds to the T3 `Interface:` line in plan.md (verbatim):
//   `WarmupResult prewarmPools(const PoolWarmupConfig& config) noexcept;`
//   returning `ok=true` with `threadsReady` count on success, else
//   `err(StartupError{ code, phase="thread-pools", message })`; plus `bool
//   creationAllowedForPhase(ResourceKind kind, int phase) noexcept;`
//   denying thread/handle creation before its declared phase.
//
// Hermetic: C++20 + `core/startup/pool_warmup.h` only. No env/config reads,
// no filesystem, no sockets, no live services, no wall-clock reads in the
// test itself (readiness is asserted from the returned WarmupResult value).
//
// ASSUMPTIONS (contract genuinely silent on these details):
//   A1: namespace is `muse::startup` (mirrors batch-1 `muse::startup`).
//   A2: `PoolWarmupConfig` is default-constructible (pool sizing comes from
//       the NFR-CORE-010-owned pool defaults, not from startup flags).
//   A3 (A3 pool owned by CORE-010): `WarmupResult` exposes `{ ok,
//       threadsReady, error }` where `threadsReady` is the observable pool
//       count and `error` reuses the T1 `StartupError{ code, phase,
//       message }` shape with `phase` = thread-pools phase index (1) and
//       `message` naming "thread-pools"; the test accepts the hyphenated
//       `thread-pools` or spaced `thread pools` spelling in the message.
//   A4: `ResourceKind` exposes thread and handle kinds as
//       `ResourceKind::kThread` / `ResourceKind::kHandle`, and the
//       thread-pools phase index is 1 (batch-1 fixed order:
//       memory/allocator=0, thread pools=1, ...).

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "core/startup/pool_warmup.h"

namespace {

using muse::startup::creationAllowedForPhase;
using muse::startup::PoolWarmupConfig;
using muse::startup::prewarmPools;
using muse::startup::ResourceKind;
using muse::startup::WarmupResult;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

bool message_names_thread_pools(const char* msg) {
  if (msg == nullptr || msg[0] == '\0') return false;
  return std::strstr(msg, "thread-pools") != nullptr ||
         std::strstr(msg, "thread pools") != nullptr;
}

bool check_prewarm_success_reports_threads_ready() {
  // Plan acceptance: after prewarmPools, pool counts are observable;
  // success returns ok=true with a threadsReady count.
  PoolWarmupConfig cfg{};
  WarmupResult r = prewarmPools(cfg);
  CHECK(r.ok);
  if (!r.ok) return false;
  CHECK(r.threadsReady > 0);
  CHECK(r.error.code == 0);
  return g_failed_checks == 0;
}

bool check_prewarm_counts_deterministic() {
  // AC-CORE-005: deterministic startup — repeated pre-warm yields the
  // same observable pool count.
  PoolWarmupConfig cfg{};
  WarmupResult a = prewarmPools(cfg);
  WarmupResult b = prewarmPools(cfg);
  CHECK(a.ok && b.ok);
  if (!a.ok || !b.ok) return false;
  CHECK(a.threadsReady == b.threadsReady);
  return g_failed_checks == 0;
}

bool check_gate_denies_thread_before_declared_phase() {
  // AC-CORE-033: thread creation before the thread-pools phase is denied.
  CHECK(!creationAllowedForPhase(ResourceKind::kThread, 0));
  return g_failed_checks == 0;
}

bool check_gate_denies_handle_before_declared_phase() {
  // AC-CORE-033: handle creation before its declared phase is denied.
  CHECK(!creationAllowedForPhase(ResourceKind::kHandle, 0));
  return g_failed_checks == 0;
}

bool check_gate_allows_at_and_after_declared_phase() {
  // Creation at/after the declared thread-pools phase (index 1) is allowed,
  // so pre-warmed first dispatch pays no creation latency.
  CHECK(creationAllowedForPhase(ResourceKind::kThread, 1));
  CHECK(creationAllowedForPhase(ResourceKind::kHandle, 1));
  CHECK(creationAllowedForPhase(ResourceKind::kThread, 5));
  return g_failed_checks == 0;
}

bool check_prewarm_failure_names_thread_pools_phase() {
  // Plan Interface: failure returns err(StartupError{ code,
  // phase="thread-pools", message }). Only exercised when the
  // implementation reports a failure; on success the shape is vacuously
  // satisfied (same pattern as batch-1 failure-shape case).
  PoolWarmupConfig cfg{};
  WarmupResult r = prewarmPools(cfg);
  if (r.ok) {
    CHECK(r.error.code == 0);
    return g_failed_checks == 0;
  }
  CHECK(r.error.code != 0);
  CHECK(r.error.phase == 1);
  CHECK(message_names_thread_pools(r.error.message));
  return g_failed_checks == 0;
}

bool check_noexcept_signatures() {
  // Compile-time: both entry points are noexcept.
  static_assert(noexcept(prewarmPools(PoolWarmupConfig{})),
                "prewarmPools must be noexcept");
  static_assert(noexcept(creationAllowedForPhase(ResourceKind::kThread, 0)),
                "creationAllowedForPhase must be noexcept");
  PoolWarmupConfig cfg{};
  WarmupResult r = prewarmPools(cfg);
  (void)r;
  (void)creationAllowedForPhase(ResourceKind::kThread, 1);
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
  RUN_CASE(check_prewarm_success_reports_threads_ready);
  RUN_CASE(check_prewarm_counts_deterministic);
  RUN_CASE(check_gate_denies_thread_before_declared_phase);
  RUN_CASE(check_gate_denies_handle_before_declared_phase);
  RUN_CASE(check_gate_allows_at_and_after_declared_phase);
  RUN_CASE(check_prewarm_failure_names_thread_pools_phase);
  RUN_CASE(check_noexcept_signatures);
  if (failures > 0) {
    std::printf("pool_warmup_test: %d failing\n", failures);
    return 1;
  }
  std::printf("pool_warmup_test: all passing\n");
  return 0;
}
