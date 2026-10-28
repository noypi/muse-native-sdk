#include "muse/state/retry_policy.hpp"

namespace muse {
namespace state {

// Retryable-failure sentinel shared with the hermetic fakes: Generation 0 is
// never a valid generation, so a response carrying it signals a transport
// failure the policy may retry.
namespace {

constexpr Generation kRetryableGeneration = 0;

bool IsRetryable(const Response& r) {
  return r.generation == kRetryableGeneration;
}

}  // namespace

StatusOr<Value> fetchGuarded(QueryKey k, Generation g, const RetryPolicy& p,
                             Transport* t) {
  const IdempotencyHint hint = t->idempotency();
  const bool idempotent = (hint == IdempotencyHint::kIdempotent);

  // Attempts used so far against the retry budget (initial attempt + retries).
  std::uint32_t attempts = 0;
  // Stale responses dropped so far. A persistently stale idempotent transport
  // would otherwise refetch forever (the stale path consumes no retry
  // budget), so stale refetches are capped: on exhaustion the fetch fails
  // with kNotFound instead of hanging.
  std::uint32_t staleDrops = 0;
  constexpr std::uint32_t kStaleBound = 3;
  for (;;) {
    Response res = t->get(k, hint);
    if (IsRetryable(res)) {
      // Non-idempotent payloads must never be retried: a duplicate effect
      // cannot be absorbed, so surface kNonIdempotent after one attempt.
      if (!idempotent) {
        return StatusOr<Value>(Status::kNonIdempotent);
      }
      if (attempts >= p.maxRetries) {
        return StatusOr<Value>(Status::kNotFound);
      }
      ++attempts;
      continue;
    }
    // Stale generations are dropped, never returned: refetch within the
    // stale bound. Equal counts as current (accepted).
    if (res.generation < g) {
      if (!idempotent) {
        return StatusOr<Value>(Status::kNonIdempotent);
      }
      if (staleDrops >= kStaleBound) {
        return StatusOr<Value>(Status::kNotFound);
      }
      ++staleDrops;
      continue;
    }
    return StatusOr<Value>(res.value);
  }
}

}  // namespace state
}  // namespace muse
