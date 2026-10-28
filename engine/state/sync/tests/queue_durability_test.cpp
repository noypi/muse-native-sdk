// Batch 1 (FR-STATE-005) contract tests — T1 durable mutation queue.
// Spec refs: FR-STATE-005, AC-STATE-021 (TC-STATE-021).
// Hermetic: C++17 + committed engine/state headers only; in-memory
// DurableStore fake in-test; no env/config/network/live service.
// Binds ONLY to the T1 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T1.md):
//   A-MQ1: namespace muse::state; Mutation/IdempotencyKey from store.hpp.
//   A-MQ2: class MutationQueue { explicit MutationQueue(DurableStore* s); ... }
//   A-MQ3: DurableStore seam { virtual Status persist(Mutation)=0;
//           virtual StatusOr<vector<Mutation>> load()=0; virtual dtor }.
//   A-MQ4: empty next() -> Status::kNotFound; ack unknown key -> kNotFound.
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/mutation_queue.hpp"
#include "muse/state/store.hpp"

namespace {

using muse::state::DurableStore;
using muse::state::IdempotencyKey;
using muse::state::Mutation;
using muse::state::MutationQueue;
using muse::state::Status;

class FakeDurableStore : public DurableStore {
 public:
  Status persist(Mutation m) override {
    persisted_.push_back(m);
    return Status::kOk;
  }
  muse::state::StatusOr<std::vector<Mutation>> load() override {
    return persisted_;
  }
  std::vector<Mutation> persisted_;
};

static Mutation makeMutation(int key, int val) {
  Mutation m;
  m.key.value = static_cast<std::uint64_t>(key);
  m.inverse.prior = muse::state::TypedValue(val);
  return m;
}

static bool check_enqueue_then_next_returns_fifo() {
  FakeDurableStore s;
  MutationQueue q(&s);
  if (q.enqueue(makeMutation(1, 10)) != Status::kOk) return false;
  if (q.enqueue(makeMutation(2, 20)) != Status::kOk) return false;
  auto first = q.next();
  if (!first.ok() || first.value().key.value != 1) return false;
  if (q.ack(IdempotencyKey{1}) != Status::kOk) return false;
  auto second = q.next();
  if (!second.ok() || second.value().key.value != 2) return false;
  return true;
}

static bool check_duplicate_enqueue_of_seen_key_is_ok_without_duplicating() {
  FakeDurableStore s;
  MutationQueue q(&s);
  if (q.enqueue(makeMutation(7, 1)) != Status::kOk) return false;
  if (q.enqueue(makeMutation(7, 1)) != Status::kOk) return false;
  auto reloaded = q.reload();
  if (!reloaded.ok()) return false;
  int count = 0;
  for (const auto& m : reloaded.value()) {
    if (m.key.value == 7) ++count;
  }
  return count == 1;
}

static bool check_ack_unknown_key_returns_not_found() {
  FakeDurableStore s;
  MutationQueue q(&s);
  if (q.ack(IdempotencyKey{999}) == Status::kOk) return false;
  return q.ack(IdempotencyKey{999}) == Status::kNotFound;
}

static bool check_next_on_empty_returns_not_found() {
  FakeDurableStore s;
  MutationQueue q(&s);
  auto r = q.next();
  return !r.ok() && r.status() == Status::kNotFound;
}

static bool check_reload_replays_fifo_after_destroy() {
  FakeDurableStore s;
  {
    MutationQueue q(&s);
    for (int i = 1; i <= 3; ++i) {
      if (q.enqueue(makeMutation(i, i * 10)) != Status::kOk) return false;
    }
  }
  MutationQueue q2(&s);  // simulates process-death destroy-and-reload
  auto reloaded = q2.reload();
  if (!reloaded.ok() || reloaded.value().size() != 3) return false;
  for (int i = 0; i < 3; ++i) {
    if (reloaded.value()[static_cast<std::size_t>(i)].key.value !=
        static_cast<std::uint64_t>(i + 1)) {
      return false;
    }
  }
  return true;
}

// Kill with N pending, reload offline, reconnect: each mutation reaches the
// fake server exactly once (server-visible dedup by key).
static bool check_reconnect_delivers_each_mutation_exactly_once() {
  FakeDurableStore s;
  {
    MutationQueue q(&s);
    for (int i = 1; i <= 5; ++i) {
      if (q.enqueue(makeMutation(i, i)) != Status::kOk) return false;
      if (q.enqueue(makeMutation(i, i)) != Status::kOk) return false;  // retry
    }
  }
  MutationQueue q2(&s);
  auto reloaded = q2.reload();
  if (!reloaded.ok()) return false;
  std::unordered_set<std::uint64_t> delivered;
  for (const auto& m : reloaded.value()) {
    if (!delivered.insert(m.key.value).second) return false;  // duplicate
  }
  if (delivered.size() != 5) return false;
  for (const auto& m : reloaded.value()) {
    if (q2.ack(m.key) != Status::kOk) return false;
  }
  return !q2.next().ok();
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_enqueue_then_next_returns_fifo);
  RUN_CASE(check_duplicate_enqueue_of_seen_key_is_ok_without_duplicating);
  RUN_CASE(check_ack_unknown_key_returns_not_found);
  RUN_CASE(check_next_on_empty_returns_not_found);
  RUN_CASE(check_reload_replays_fifo_after_destroy);
  RUN_CASE(check_reconnect_delivers_each_mutation_exactly_once);
  if (failures > 0) {
    std::printf("queue_durability_test: %d failing\n", failures);
    return 1;
  }
  std::printf("queue_durability_test: all passing\n");
  return 0;
}
