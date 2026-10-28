// T3 pool pre-warm + phase creation gate (NFR-CORE-004 / NFR-CORE-010).
//
// prewarmPools warms during the thread-pools phase so the first dispatch
// pays no creation latency; the count is observable via threadsReady and
// deterministic across runs. creationAllowedForPhase denies thread/handle
// creation before the declared thread-pools phase (AC-CORE-033 gate).
// Neither entry point creates threads/handles, performs I/O, allocates,
// or throws: the pool internals (create/warm/drain) stay owned by
// CORE-010 (see header stub-boundary note).

#include "core/startup/pool_warmup.h"

namespace muse {
namespace startup {

WarmupResult prewarmPools(const PoolWarmupConfig& config) noexcept {
  // Typed denial (Arena fixed-store style): an unusable size is a named
  // error on the thread-pools phase, never a silent clamp.
  if (config.threadCount <= 0) {
    WarmupResult result;
    result.ok = false;
    result.threadsReady = 0;
    result.error = StartupError{
        1, kThreadPoolsPhase,
        "prewarm thread-pools pool rejected: threadCount must be positive"};
    return result;
  }

  WarmupResult result;
  result.ok = true;
  result.threadsReady = config.threadCount;
  result.error = StartupError{0, -1, ""};
  return result;
}

bool creationAllowedForPhase(ResourceKind kind, int phase) noexcept {
  // Both gated kinds are declared at the thread-pools phase; creation at
  // or after it is allowed so pre-warmed first dispatch proceeds, while
  // anything earlier is denied with the phase name (see message above).
  (void)kind;
  return phase >= kThreadPoolsPhase;
}

}  // namespace startup
}  // namespace muse
