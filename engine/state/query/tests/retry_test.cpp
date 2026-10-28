// Batch 3 contract tests — T6, bounded idempotent retry.
// Spec refs: FR-STATE-004, AC-STATE-019 (TC-STATE-019).
// Hermetic: fake Transport in-test; no env/config/network.
// Binds ONLY to the T6 `Interface:` line; see unit-tests/T6.md assumptions.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/query_client.hpp"
#include "muse/state/retry_policy.hpp"

namespace {

using muse::state::BackoffCurve;
using muse::state::Generation;
using muse::state::IdempotencyHint;
using muse::state::QueryKey;
using muse::state::Response;
using muse::state::RetryPolicy;
using muse::state::Status;
using muse::state::Transport;
using muse::state::Value;

// Fails exactly failTimes_ times (transport-level failure signalled via a
// Response the policy treats as retryable — here Generation 0 sentinel on a
// fake that counts attempts), then succeeds.
class FlakyTransport : public Transport {
 public:
  explicit FlakyTransport(int failTimes) : failTimes_(failTimes) {}
  int attempts = 0;
  std::vector<IdempotencyHint> hints;
  IdempotencyHint idempotency() const override {
    return IdempotencyHint::kIdempotent;
  }
  Response get(QueryKey /*k*/, IdempotencyHint h) override {
    ++attempts;
    hints.push_back(h);
    if (attempts <= failTimes_) return Response{Value(-1), kRetryable};
    return Response{Value(9), /*generation=*/1};
  }
  static constexpr uint64_t kRetryable = 0;

 private:
  int failTimes_;
};

static RetryPolicy policy(uint32_t maxRetries) {
  RetryPolicy p;
  p.maxRetries = maxRetries;
  p.backoff = BackoffCurve{};
  return p;
}

// Named case: retries stop at maxRetries — transport called exactly
// 1 + maxRetries times, then the failure surfaces.
static bool check_retries_stop_at_max_retries() {
  FlakyTransport t(100);  // always fails
  auto r = fetchGuarded(QueryKey("k"), Generation(0), policy(3), &t);
  if (r.ok()) return false;
  return t.attempts == 4;  // 1 initial + 3 retries
}

// Named case: success within budget returns the value with no further calls.
static bool check_success_within_budget_returns_value() {
  FlakyTransport t(2);
  auto r = fetchGuarded(QueryKey("k"), Generation(0), policy(3), &t);
  if (!r.ok()) return false;
  if (!(r.value() == Value(9))) return false;
  return t.attempts == 3;
}

// Named case: every retry passes the transport's IdempotencyHint through.
static bool check_retries_pass_idempotency_hint() {
  FlakyTransport t(1);
  auto r = fetchGuarded(QueryKey("k"), Generation(0), policy(2), &t);
  if (!r.ok()) return false;
  if (t.hints.empty()) return false;
  for (auto h : t.hints) {
    if (h != IdempotencyHint::kIdempotent) return false;
  }
  return true;
}

// Named case: non-idempotent hints disable retry — exactly one attempt and
// Status::kNonIdempotent.
static bool check_non_idempotent_hint_disables_retry() {
  struct NonIdempotent : public Transport {
    int attempts = 0;
    IdempotencyHint idempotency() const override {
      return IdempotencyHint::kNonIdempotent;
    }
    Response get(QueryKey /*k*/, IdempotencyHint /*h*/) override {
      ++attempts;
      return Response{Value(-1), FlakyTransport::kRetryable};
    }
  };
  NonIdempotent t;
  // The transport declares its payload non-idempotent via idempotency():
  // the guarded fetch must refuse to retry (exactly one attempt).
  auto r = fetchGuarded(QueryKey("k"), Generation(0), policy(3), &t);
  if (r.ok()) return false;
  if (r.status() != Status::kNonIdempotent) return false;
  return t.attempts == 1;
}

// Named case: maxRetries == 0 boundary — exactly one attempt, no retry.
static bool check_zero_max_retries_single_attempt() {
  FlakyTransport t(1);
  auto r = fetchGuarded(QueryKey("k"), Generation(0), policy(0), &t);
  if (r.ok()) return false;
  return t.attempts == 1;
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_retries_stop_at_max_retries);
  RUN_CASE(check_success_within_budget_returns_value);
  RUN_CASE(check_retries_pass_idempotency_hint);
  RUN_CASE(check_non_idempotent_hint_disables_retry);
  RUN_CASE(check_zero_max_retries_single_attempt);
  if (failures > 0) {
    std::printf("retry_test: %d failing\n", failures);
    return 1;
  }
  std::printf("retry_test: all passing\n");
  return 0;
}
