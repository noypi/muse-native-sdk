// Batch 3 contract tests — T6, generation staleness guard.
// Spec refs: FR-STATE-004, AC-STATE-018 (TC-STATE-018).
// Hermetic: fake Transport in-test; no env/config/network.
// Binds ONLY to the T6 `Interface:` line; see unit-tests/T6.md assumptions.
#include <cstdio>
#include <string>

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

static RetryPolicy noRetry() {
  RetryPolicy p;
  p.maxRetries = 0;
  p.backoff = BackoffCurve{};
  return p;
}

class StaleThenFresh : public Transport {
 public:
  IdempotencyHint idempotency() const override {
    return IdempotencyHint::kIdempotent;
  }
  Response get(QueryKey /*k*/, IdempotencyHint /*h*/) override {
    // First response carries a stale generation, second is current.
    if (!served_) {
      served_ = true;
      return Response{Value(1), /*generation=*/1};
    }
    return Response{Value(2), /*generation=*/3};
  }

 private:
  bool served_ = false;
};

// Named case: stale Generation responses are dropped, never written —
// fetchGuarded with a newer Generation ignores the stale payload.
static bool check_stale_generation_response_dropped_never_written() {
  StaleThenFresh t;
  // Cache holds generation 3; transport first answers generation 1 (stale).
  auto r = fetchGuarded(QueryKey("k"), Generation(3), noRetry(), &t);
  if (!r.ok()) return false;
  return r.value() == Value(2);
}

// Named case: current-generation response is accepted and returned.
static bool check_current_generation_response_accepted() {
  StaleThenFresh t;
  auto r = fetchGuarded(QueryKey("k"), Generation(0), noRetry(), &t);
  if (!r.ok()) return false;
  return r.value() == Value(1);
}

// Named case: equal-generation (boundary) response is accepted.
static bool check_equal_generation_response_accepted() {
  StaleThenFresh t;
  auto r = fetchGuarded(QueryKey("k"), Generation(1), noRetry(), &t);
  if (!r.ok()) return false;
  return r.value() == Value(1);
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
  RUN_CASE(check_stale_generation_response_dropped_never_written);
  RUN_CASE(check_current_generation_response_accepted);
  RUN_CASE(check_equal_generation_response_accepted);
  if (failures > 0) {
    std::printf("generation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("generation_test: all passing\n");
  return 0;
}
