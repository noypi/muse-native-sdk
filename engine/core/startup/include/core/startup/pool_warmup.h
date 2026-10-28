// Pre-warmed worker pools + phase creation gate (NFR-CORE-004 / NFR-CORE-010).
//
// Warms the worker pool owned by NFR-CORE-010/FR-CORE-003 during the
// thread-pools bootstrap phase (index 1) so first dispatches pay no
// creation latency. The observable pool count is returned via
// WarmupResult.threadsReady and is deterministic across runs.
//
// NOTE (stub boundary): the pool implementation (core/thread/worker_pool.h)
// currently exposes only WorkerPool(n)/submit/shutdown and no
// create/warm entry point for startup to call. Per the T3 plan detail the
// pool internals are owned by CORE-010, so this file must not reimplement
// the pool: prewarmPools records declared-phase readiness deterministically
// (no thread/handle creation, no I/O, no allocation) until the CORE-010
// warm hook lands, at which point only the body of prewarmPools changes.
// The gate (creationAllowedForPhase) denies thread/handle creation before
// its declared phase with the phase name, per AC-CORE-033.
#pragma once

#include <cstddef>

#include "core/startup/bootstrap.h"

namespace muse {
namespace startup {

// Declared phase index of the thread-pools bootstrap phase, aligned with
// kBootstrapPhaseNames (memory/allocator=0, thread pools=1, ...).
inline constexpr int kThreadPoolsPhase = 1;

// Default worker count used when the caller leaves PoolWarmupConfig at
// its defaults. Sizing itself stays with the NFR-CORE-010-owned pool;
// this default only fixes the observable stub count.
inline constexpr int kDefaultWarmThreads = 4;

// Pool warmup configuration. Default-constructible; sizing comes from
// the NFR-CORE-010-owned pool defaults, not from startup flags.
struct PoolWarmupConfig {
  int threadCount = kDefaultWarmThreads;
};

// Warmup outcome. On success ok=true with the observable threadsReady
// count; on failure ok=false with a named StartupError whose phase is
// the thread-pools index and whose message names the thread-pools phase.
struct WarmupResult {
  bool ok = false;
  int threadsReady = 0;
  StartupError error;
};

// Resource kinds gated by creationAllowedForPhase.
enum class ResourceKind {
  kThread,
  kHandle,
};

// Pre-warm worker pools at their declared (thread-pools) phase. Never
// throws. Deterministic: identical configs yield identical threadsReady.
WarmupResult prewarmPools(const PoolWarmupConfig& config) noexcept;

// Phase gate: thread/handle creation is denied before its declared phase
// (thread-pools, index 1) and allowed at/after it. Never throws.
bool creationAllowedForPhase(ResourceKind kind, int phase) noexcept;

}  // namespace startup
}  // namespace muse
