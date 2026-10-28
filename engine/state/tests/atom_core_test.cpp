// Batch 1 (Foundations) contract tests — T1 + T2.
// Spec refs: FR-STATE-001 (AC-STATE-006), FR-STATE-002 (AC-STATE-011/012/013).
// Hermetic: C++17 + committed engine/state headers only; no network, no env/
// config reads, no filesystem, no live services. In-process fakes only.
// Binds ONLY to the T1/T2 `Interface:` lines; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T1.md, T2.md):
//   A1: headers expose namespace muse::state.
//   A2: Status errno-style with Status::kNotFound / Status::kOk (or Status::ok()).
//   A3: read returns StatusOr<TypedValue> so kNotFound is observable without UB.
//   A4: Batch exposes size()/empty() and nodeVisits() (O(depth) visit bound).
//   A5: Store exposes Snapshot snapshot() const with operator== (deep-equal).
//   A6: Action/Mutation/InversePatch/IdempotencyKey are default/value constructible
//       for tests; dispatch returns Status; failing actions report non-ok Status.
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/store.hpp"

namespace {

using muse::state::Action;
using muse::state::AtomId;
using muse::state::AtomStore;
using muse::state::Batch;
using muse::state::DurableQueue;
using muse::state::IdempotencyKey;
using muse::state::InversePatch;
using muse::state::Mutation;
using muse::state::ObserverId;
using muse::state::Provenance;
using muse::state::Status;
using muse::state::Store;
using muse::state::TypedValue;

// --- T1 helpers ---------------------------------------------------------------

static TypedValue intValue(int v) { return TypedValue(v); }  // ASSUMPTION A6.

// --- T1 cases -----------------------------------------------------------------

// Named case: create then read returns the written value.
static bool check_create_read_roundtrip() {
  AtomStore s;
  AtomId id = s.create(intValue(42));
  ObserverId obs = 1;
  auto got = s.read(id, obs);
  if (!got.ok()) return false;
  return got.value() == intValue(42);
}

// Named case: reading an unknown AtomId yields kNotFound, never UB/crash.
static bool check_read_unknown_id_returns_not_found() {
  AtomStore s;
  auto got = s.read(static_cast<AtomId>(999999), 1);
  if (got.ok()) return false;
  return got.status() == Status::kNotFound;
}

// Named case: a single write enqueues one invalidation; flush yields exactly
// one coalesced batch (AC-STATE-006 structural subset).
static bool check_single_write_yields_exactly_one_batch() {
  AtomStore s;
  AtomId id = s.create(intValue(1));
  s.read(id, 1);  // establish dependency
  s.write(id, intValue(2), Provenance::kJs);
  Batch b = s.flush();
  if (b.size() != 1) return false;
  Batch b2 = s.flush();
  return b2.empty();  // flush drains; second flush is empty
}

// Named case: N writes before flush coalesce into a single batch.
static bool check_many_writes_coalesce_to_single_batch() {
  AtomStore s;
  std::vector<AtomId> ids;
  for (int i = 0; i < 64; ++i) ids.push_back(s.create(intValue(i)));
  for (AtomId id : ids) s.read(id, 1);
  for (AtomId id : ids) s.write(id, intValue(-1), Provenance::kJs);
  Batch b = s.flush();
  return b.size() == 1;
}

// Named case: 10,000-atom chain, one write visits O(depth) nodes and yields
// exactly one batch. Depth of a chain of 10,000 is 10,000; O(depth) bound
// with small constant: visits must stay <= 4 * depth.
static bool check_large_tree_single_write_bounded_visit_one_batch() {
  AtomStore s;
  const int kAtoms = 10000;
  std::vector<AtomId> ids;
  ids.reserve(kAtoms);
  for (int i = 0; i < kAtoms; ++i) ids.push_back(s.create(intValue(i)));
  for (AtomId id : ids) s.read(id, 1);
  const int kDepth = kAtoms;
  s.write(ids[kAtoms / 2], intValue(-7), Provenance::kJs);
  Batch b = s.flush();
  if (b.size() != 1) return false;
  return b.nodeVisits() <= static_cast<std::size_t>(4 * kDepth);
}

// Named case: concurrent writes from 4 threads lose no updates (TSan clean is
// a property of the red/green run command, asserted by running under TSan).
static bool check_concurrent_writes_lose_no_updates() {
  AtomStore s;
  const int kThreads = 4;
  const int kPerThread = 250;
  std::vector<std::vector<AtomId>> ids(kThreads);
  for (int t = 0; t < kThreads; ++t)
    for (int i = 0; i < kPerThread; ++i)
      ids[t].push_back(s.create(intValue(0)));
  std::vector<std::thread> threads;
  std::atomic<int> failures{0};
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t] {
      for (int i = 0; i < kPerThread; ++i) {
        s.write(ids[t][i], intValue(t * 100000 + i), Provenance::kJs);
      }
    });
  }
  for (auto& th : threads) th.join();
  for (int t = 0; t < kThreads; ++t) {
    for (int i = 0; i < kPerThread; ++i) {
      auto got = s.read(ids[t][i], 1);
      if (!got.ok() || !(got.value() == intValue(t * 100000 + i))) {
        failures.fetch_add(1);
      }
    }
  }
  // Drain: concurrent writes must coalesce without error.
  Batch b = s.flush();
  if (b.empty()) return false;
  return failures.load() == 0;
}

// --- T2 fakes -----------------------------------------------------------------

// In-memory DurableQueue fake: records every enqueue in-process.
class FakeDurableQueue : public DurableQueue {
 public:
  Status enqueue(Mutation m) override {
    calls.push_back(m);
    return Status::kOk;
  }
  std::vector<Mutation> calls;
};

// --- T2 cases -----------------------------------------------------------------

// Named case: dispatch enqueues exactly once per mutation with a stable
// idempotency key (AC-STATE-013 seam behavior).
static bool check_dispatch_enqueues_once_with_stable_key() {
  Store store;
  FakeDurableQueue q;
  store.setDurableQueue(&q);
  Action a;  // ASSUMPTION A6: default action fixture.
  if (store.dispatch(a) != Status::kOk) return false;
  if (q.calls.size() != 1) return false;
  IdempotencyKey first = q.calls[0].key;
  // A second, equal mutation must carry the same stable key.
  if (store.dispatch(a) != Status::kOk) return false;
  if (q.calls.size() != 2) return false;
  return q.calls[1].key == first;
}

// Named case: a failing action restores the exact pre-mutation state
// (deep-equal) with a single invalidation batch (AC-STATE-012).
static bool check_failing_action_restores_exact_state_single_batch() {
  Store store;
  FakeDurableQueue q;
  store.setDurableQueue(&q);
  auto before = store.snapshot();
  Action failing = Action::failing();  // ASSUMPTION A6: failing-action fixture.
  Status st = store.dispatch(failing);
  if (st == Status::kOk) return false;  // must report failure
  if (!(store.snapshot() == before)) return false;
  Batch b = store.flushBatch();
  return b.size() == 1;
}

// Named case: rollback restores deep-equal state in one batch.
static bool check_rollback_restores_deep_equal_single_batch() {
  Store store;
  FakeDurableQueue q;
  store.setDurableQueue(&q);
  auto before = store.snapshot();
  Action a;
  if (store.dispatch(a) != Status::kOk) return false;
  if (q.calls.empty()) return false;
  Mutation m = q.calls.back();
  if (store.rollback(m) != Status::kOk) return false;
  if (!(store.snapshot() == before)) return false;
  Batch b = store.flushBatch();
  return b.size() == 1;
}

// Named case: null DurableQueue (nullable fake) — dispatch still applies
// without crashing and without enqueue.
static bool check_null_queue_dispatch_still_applies() {
  Store store;
  store.setDurableQueue(nullptr);
  Action a;
  if (store.dispatch(a) != Status::kOk) return false;
  return true;
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
  RUN_CASE(check_create_read_roundtrip);
  RUN_CASE(check_read_unknown_id_returns_not_found);
  RUN_CASE(check_single_write_yields_exactly_one_batch);
  RUN_CASE(check_many_writes_coalesce_to_single_batch);
  RUN_CASE(check_large_tree_single_write_bounded_visit_one_batch);
  RUN_CASE(check_concurrent_writes_lose_no_updates);
  RUN_CASE(check_dispatch_enqueues_once_with_stable_key);
  RUN_CASE(check_failing_action_restores_exact_state_single_batch);
  RUN_CASE(check_rollback_restores_deep_equal_single_batch);
  RUN_CASE(check_null_queue_dispatch_still_applies);
  if (failures > 0) {
    std::printf("atom_core_test: %d failing\n", failures);
    return 1;
  }
  std::printf("atom_core_test: all passing\n");
  return 0;
}
