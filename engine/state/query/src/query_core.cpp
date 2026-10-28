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
    // Stale generations are dropped, never returned: refetch without
    // consuming the retry budget. Equal counts as current (accepted).
    if (res.generation < g) {
      if (!idempotent) {
        return StatusOr<Value>(Status::kNonIdempotent);
      }
      continue;
    }
    return StatusOr<Value>(res.value);
  }
}

}  // namespace state
}  // namespace muse
