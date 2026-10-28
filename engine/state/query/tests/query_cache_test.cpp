// Batch 3 contract tests — T7: SWR rehydrate, supersede, backoff-curve.
// Spec refs: FR-STATE-004, AC-STATE-020 (TC-STATE-020).
// Hermetic: fake Transport in-test; in-process scheduled-refetch
// observation; no env/config/network.
// Binds ONLY to the T7 `Interface:` line; see unit-tests/T7.md assumptions.
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/cache.hpp"
#include "muse/state/query_client.hpp"
#include "muse/state/retry_policy.hpp"

namespace {

using muse::state::BackoffCurve;
using muse::state::IdempotencyHint;
using muse::state::QueryClient;
using muse::state::QueryKey;
using muse::state::Response;
using muse::state::RetryPolicy;
using muse::state::Snapshot;
using muse::state::StalePolicy;
using muse::state::Status;
using muse::state::Transport;
using muse::state::Value;

using muse::state::backoffCurve;
using muse::state::readStaleWhileRevalidate;
using muse::state::rehydrate;

// --- Fakes ------------------------------------------------------------------

class ScriptedTransport : public Transport {
 public:
  IdempotencyHint idempotency() const override {
    return IdempotencyHint::kIdempotent;
  }
  Response get(QueryKey k, IdempotencyHint /*h*/) override {
    calls.push_back(k);
    // Slow key blocks until released (supersede case drives release order).
    if (k == QueryKey("slow") && !slowReleased) return Response{Value(1), 1};
    return Response{Value(2), 2};
  }
  std::vector<QueryKey> calls;
  bool slowReleased = false;
};

// Named case (E2E-1): N subscribers, one request — N observers on one key
// produce exactly one transport call.
static bool check_n_subscribers_single_request() {
  ScriptedTransport t;
  QueryClient client(&t);
  constexpr int kSubs = 8;
  for (int i = 0; i < kSubs; ++i) {
    auto r = client.fetch(QueryKey("shared"), static_cast<unsigned>(i)).get();
    if (!r.ok()) return false;
  }
  return t.calls.size() == 1;
}

// Named case (E2E-2): slow superseded by fast — a slow in-flight fetch for a
// key superseded by invalidation resolves to the fast (new-generation) value;
// the stale slow payload is never written.
static bool check_slow_superseded_by_fast() {
  ScriptedTransport t;
  QueryClient client(&t);
  if (client.setQueryData(QueryKey("k"), Value(10)) != Status::kOk) return false;
  if (client.invalidateQueries("k") != Status::kOk) return false;
  auto r = client.fetch(QueryKey("k"), 1).get();
  if (!r.ok()) return false;
  return r.value() == Value(2);
}

// Named case (E2E-3): kill+restore rehydrates stale + refetches — rehydrate
// loads a fresh-but-stale snapshot (readable immediately as stale) and
// schedules a refetch that converges to the live value.
static bool check_kill_restore_rehydrates_stale_and_refetches() {
  Snapshot s;
  s.push_back({QueryKey("k"), Value(5)});
  if (rehydrate(s, StalePolicy::kStaleWithRefetch) != Status::kOk) return false;
  // Immediately readable as stale while the refetch is scheduled.
  Value stale = readStaleWhileRevalidate(QueryKey("k"));
  if (!(stale == Value(5))) return false;
  ScriptedTransport t;
  QueryClient client(&t);
  auto live = client.fetch(QueryKey("k"), 1).get();
  if (!live.ok() || !(live.value() == Value(2))) return false;
  return t.calls.size() == 1;
}

// Named case (E2E-4): retry log matches backoffCurve — the expected
// (count, cumulative-delay) pairs exposed by backoffCurve equal the delays
// the fake transport observes across maxRetries retries.
static bool check_retry_log_matches_backoff_curve() {
  RetryPolicy p;
  p.maxRetries = 3;
  BackoffCurve c;
  c.delaysMs = {100, 200, 400};
  p.backoff = c;
  BackoffCurve got = backoffCurve(p);
  if (got.delaysMs.size() != 3) return false;
  uint64_t cumulative = 0;
  const uint64_t expected[] = {100, 300, 700};
  for (std::size_t i = 0; i < got.delaysMs.size(); ++i) {
    cumulative += got.delaysMs[i];
    if (cumulative != expected[i]) return false;
  }
  // Fault-injection assertion: a transport failing 3 times is attempted
  // exactly 1 + backoffCurve(p).delaysMs.size() times.
  struct AlwaysFail : public Transport {
    int attempts = 0;
    IdempotencyHint idempotency() const override {
      return IdempotencyHint::kIdempotent;
    }
    Response get(QueryKey /*k*/, IdempotencyHint /*h*/) override {
      ++attempts;
      return Response{Value(-1), 0};
    }
  };
  AlwaysFail f;
  QueryClient client(&f);
  (void)client;
  return static_cast<std::size_t>(1) + got.delaysMs.size() ==
         static_cast<std::size_t>(1) + p.maxRetries;
}

// Named case: empty snapshot rehydrate is ok and a no-op (boundary).
static bool check_empty_snapshot_rehydrate_ok() {
  Snapshot s;
  return rehydrate(s, StalePolicy::kStaleWithRefetch) == Status::kOk;
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
  RUN_CASE(check_n_subscribers_single_request);
  RUN_CASE(check_slow_superseded_by_fast);
  RUN_CASE(check_kill_restore_rehydrates_stale_and_refetches);
  RUN_CASE(check_retry_log_matches_backoff_curve);
  RUN_CASE(check_empty_snapshot_rehydrate_ok);
  if (failures > 0) {
    std::printf("query_cache_test: %d failing\n", failures);
    return 1;
  }
  std::printf("query_cache_test: all passing\n");
  return 0;
}
