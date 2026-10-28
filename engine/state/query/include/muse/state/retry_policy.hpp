#pragma once

#include <cstdint>
#include <vector>

#include "muse/state/query_client.hpp"

namespace muse {
namespace state {

// Millisecond delays, one entry per retry (T7 asserts them as data;
// fetchGuarded never sleeps on them).
struct BackoffCurve {
  std::vector<std::uint64_t> delaysMs;
};

struct RetryPolicy {
  std::uint32_t maxRetries = 0;
  BackoffCurve backoff;
};

// Guarded fetch (AC-STATE-018/019): drops responses with a stale Generation
// (older than `g`; never returned), retries retryable transport failures up to
// `p.maxRetries` (1 + maxRetries attempts total), and threads the transport's
// IdempotencyHint into every get call. A `kNonIdempotent` transport is
// attempted exactly once and surfaces Status::kNonIdempotent on failure.
// Generation 0 is the transport-level retryable-failure sentinel: it is never
// a valid generation and never satisfies a held generation.
StatusOr<Value> fetchGuarded(QueryKey k, Generation g, const RetryPolicy& p,
                             Transport* t);

}  // namespace state
}  // namespace muse
