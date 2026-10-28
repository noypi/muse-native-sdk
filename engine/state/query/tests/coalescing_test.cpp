// Batch 3 (Async Data & Query Core, FR-STATE-004) contract tests — T5.
// Spec refs: FR-STATE-004, AC-STATE-017 (TC-STATE-017).
// Hermetic: C++17 + committed engine/state headers only; fake Transport
// in-test, std::thread for the concurrency case; no env/config/network.
// Binds ONLY to the T5 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T5.md):
//   A-Q1: namespace muse::state; query types live there.
//   A-Q2: QueryKey/Tag are string-like (constructible from const char*,
//         equality-comparable); Value is TypedValue; ObserverId from atom.hpp.
//   A-Q3: Transport has virtual Response get(QueryKey, IdempotencyHint) = 0
//         with virtual dtor; Response carries Value + Generation.
//   A-Q4: QueryClient::fetch returns Future<Value> with StatusOr<Value> get().
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/query_client.hpp"

namespace {

using muse::state::Future;
using muse::state::IdempotencyHint;
using muse::state::ObserverId;
using muse::state::QueryClient;
using muse::state::QueryKey;
using muse::state::Response;
using muse::state::Status;
using muse::state::Tag;
using muse::state::Transport;
using muse::state::TypedValue;
using muse::state::Value;

// --- Fakes ------------------------------------------------------------------

class FakeTransport : public Transport {
 public:
  std::atomic<int> calls{0};
  IdempotencyHint idempotency() const override {
    return IdempotencyHint::kIdempotent;
  }
  Response get(QueryKey /*k*/, IdempotencyHint /*h*/) override {
    ++calls;
    return Response{Value(7), 1};
  }
};

// Named case: 50 concurrent identical keys → exactly 1 transport call,
// all observers resolve to the same value.
static bool check_concurrent_identical_keys_coalesce_to_single_call() {
  FakeTransport t;
  QueryClient client(&t);
  constexpr int kObservers = 50;
  std::vector<std::thread> threads;
  std::vector<int> results(kObservers, -1);
  std::vector<bool> ok(kObservers, false);
  for (int i = 0; i < kObservers; ++i) {
    threads.emplace_back([&, i] {
      Future<Value> f = client.fetch(QueryKey("user:1"), static_cast<ObserverId>(i));
      auto r = f.get();
      ok[i] = r.ok();
      if (r.ok()) results[i] = r.value().get();
    });
  }
  for (auto& th : threads) th.join();
  if (t.calls.load() != 1) return false;
  for (int i = 0; i < kObservers; ++i) {
    if (!ok[i] || results[i] != 7) return false;
  }
  return true;
}

// Named case: distinct keys do not coalesce — one transport call per key.
static bool check_distinct_keys_do_not_coalesce() {
  FakeTransport t;
  QueryClient client(&t);
  auto a = client.fetch(QueryKey("a"), 1).get();
  auto b = client.fetch(QueryKey("b"), 2).get();
  if (!a.ok() || !b.ok()) return false;
  return t.calls.load() == 2;
}

// Named case: invalidateQueries(Tag) returns ok and forces refetch
// (next fetch re-hits transport; write-through: cache never double-writes).
static bool check_invalidate_forces_refetch() {
  FakeTransport t;
  QueryClient client(&t);
  auto first = client.fetch(QueryKey("k"), 1).get();
  if (!first.ok()) return false;
  if (client.invalidateQueries(Tag("tag:k")) != Status::kOk) return false;
  auto second = client.fetch(QueryKey("k"), 1).get();
  if (!second.ok()) return false;
  return t.calls.load() == 2;
}

// Named case: setQueryData seeds the cache — fetch resolves without a
// transport call (invalidation-driven copy, write-through source).
static bool check_set_query_data_seeds_cache_without_fetch() {
  FakeTransport t;
  QueryClient client(&t);
  if (client.setQueryData(QueryKey("k"), Value(42)) != Status::kOk) return false;
  auto got = client.fetch(QueryKey("k"), 1).get();
  if (!got.ok() || got.value() != Value(42)) return false;
  return t.calls.load() == 0;
}

// Named case: cancelQueries returns ok; a cancelled key refetches on next
// fetch (in-flight waiters are released, never left hanging).
static bool check_cancel_queries_releases_and_refetches() {
  FakeTransport t;
  QueryClient client(&t);
  auto first = client.fetch(QueryKey("k"), 1).get();
  if (!first.ok()) return false;
  if (client.cancelQueries(QueryKey("k")) != Status::kOk) return false;
  auto second = client.fetch(QueryKey("k"), 2).get();
  if (!second.ok()) return false;
  return t.calls.load() == 2;
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
  RUN_CASE(check_concurrent_identical_keys_coalesce_to_single_call);
  RUN_CASE(check_distinct_keys_do_not_coalesce);
  RUN_CASE(check_invalidate_forces_refetch);
  RUN_CASE(check_set_query_data_seeds_cache_without_fetch);
  RUN_CASE(check_cancel_queries_releases_and_refetches);
  if (failures > 0) {
    std::printf("coalescing_test: %d failing\n", failures);
    return 1;
  }
  std::printf("coalescing_test: all passing\n");
  return 0;
}
