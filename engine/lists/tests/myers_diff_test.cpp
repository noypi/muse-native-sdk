// T4 contract tests — FR-LISTS-003 / AC-LISTS-010 (TC-LISTS-010;
// NFR-LISTS-002 context: diff off UI thread).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services. Worker-thread case uses std::thread
// only (no pool, no sleep, no wall-clock assertions).
// Binds to the T4 `Interface:` line:
//   `EditScript myersDiff(const KeyedList& oldL, const KeyedList& newL);
//    Batch applyBatch(Recycler& r, EditScript e);`
//   — thread-safe, atomic batch.
// Include path follows the engine convention:
//   #include "lists/myers_diff.h" with engine/lists/include on the path.
//
// ASSUMPTION (type spellings): the plan/Interface line does not settle the
// C++ namespace, the KeyedList element/field spellings, the EditScript op
// representation, the Batch shape, or the Recycler observable surface. This
// test proposes: namespace `lists`; `KeyedList{std::vector<std::string>
// keys}`; `EditType::{Insert, Remove, Move}` with `EditOp{EditType type;
// std::string key; size_t from; size_t to}`; `EditScript` as a vector of
// EditOp; `Recycler` constructible from a KeyedList, exposing
// `order()` (vector<string> snapshot) and `rebindCount()` (number of
// rebound items); `Batch` as the applied-batch record exposing at least
// the committed order. Reconciliation rule: if the coder names them
// differently, update these call sites — not the expected behavior
// (minimal scripts, worker-thread callable, exact-order atomic apply with
// minimal rebinds).
//
// ASSUMPTION (minimality counting): "minimal edit script" means the op
// count equals the list-size delta for pure insert/remove fixtures
// (one op per inserted/removed key) and equals the move count for pure
// reorder fixtures (one Move op per relocated key, no remove+insert
// pairs for a relocation). Unchanged items never appear in the script.
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "lists/myers_diff.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::KeyedList kl(std::initializer_list<const char*> ids) {
  lists::KeyedList l{};
  for (const char* id : ids) l.keys.emplace_back(id);
  return l;
}

// Applies the script through the Recycler and checks the committed order.
bool apply_is_exact(const lists::KeyedList& oldL, const lists::KeyedList& newL,
                    const lists::EditScript& script) {
  lists::Recycler r(oldL);
  lists::Batch b = lists::applyBatch(r, script);
  (void)b;
  return r.order() == newL.keys;
}

size_t op_count(const lists::EditScript& s) { return s.ops.size(); }

// TC-LISTS-010: identical lists yield an empty script; apply is a no-op
// with zero rebinds.
void case_identical_is_empty() {
  lists::KeyedList oldL = kl({"a", "b", "c"});
  lists::EditScript s = lists::myersDiff(oldL, oldL);
  check(op_count(s) == 0, "identical_is_empty");
  lists::Recycler r(oldL);
  lists::applyBatch(r, s);
  check(r.order() == oldL.keys && r.rebindCount() == 0,
        "identical_apply_noop_zero_rebinds");
}

void case_empty_to_empty() {
  lists::EditScript s = lists::myersDiff(kl({}), kl({}));
  check(op_count(s) == 0, "empty_to_empty");
}

// TC-LISTS-010: inserted fixture yields a minimal script (one op per
// inserted key) and batch apply lands the exact new order.
void case_inserted_minimal() {
  lists::KeyedList oldL = kl({"a", "b"});
  lists::KeyedList newL = kl({"a", "x", "b", "y"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  check(op_count(s) == 2, "inserted_minimal");
  check(apply_is_exact(oldL, newL, s), "inserted_apply_exact_order");
}

void case_inserted_rebinds_minimal() {
  lists::KeyedList oldL = kl({"a", "b"});
  lists::KeyedList newL = kl({"a", "x", "b"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  lists::Recycler r(oldL);
  lists::applyBatch(r, s);
  // Only the inserted item is bound; pre-existing items are not rebound.
  check(r.rebindCount() == 1, "inserted_rebinds_minimal");
}

// TC-LISTS-010: removed fixture yields a minimal script.
void case_removed_minimal() {
  lists::KeyedList oldL = kl({"a", "x", "b", "y"});
  lists::KeyedList newL = kl({"a", "b"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  check(op_count(s) == 2, "removed_minimal");
  check(apply_is_exact(oldL, newL, s), "removed_apply_exact_order");
}

// TC-LISTS-010: shuffled fixture yields move ops (no remove+insert pairs
// for a relocation) and apply lands the exact order.
void case_shuffled_uses_moves() {
  lists::KeyedList oldL = kl({"a", "b", "c", "d"});
  lists::KeyedList newL = kl({"d", "a", "b", "c"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  size_t moves = 0;
  size_t inserts = 0;
  size_t removes = 0;
  for (const auto& op : s.ops) {
    if (op.type == lists::EditType::Move) ++moves;
    if (op.type == lists::EditType::Insert) ++inserts;
    if (op.type == lists::EditType::Remove) ++removes;
  }
  check(moves == 1 && inserts == 0 && removes == 0, "shuffled_uses_moves");
  check(apply_is_exact(oldL, newL, s), "shuffled_apply_exact_order");
}

void case_swap_is_minimal() {
  lists::KeyedList oldL = kl({"a", "b", "c"});
  lists::KeyedList newL = kl({"c", "b", "a"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  check(op_count(s) == 2, "swap_is_minimal");
  check(apply_is_exact(oldL, newL, s), "swap_apply_exact_order");
}

// Mixed insert + remove + move stays minimal and exact.
void case_mixed_edits() {
  lists::KeyedList oldL = kl({"a", "b", "c", "d"});
  lists::KeyedList newL = kl({"d", "a", "e", "c"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  // d moved, b removed, e inserted -> 3 ops.
  check(op_count(s) == 3, "mixed_edits_minimal");
  check(apply_is_exact(oldL, newL, s), "mixed_apply_exact_order");
}

// NFR-LISTS-002 context: myersDiff is callable from a worker thread (no
// UI-thread affinity). Deterministic: no sleeps, join before asserting.
void case_worker_thread_callable() {
  lists::KeyedList oldL = kl({"a", "b", "c", "d"});
  lists::KeyedList newL = kl({"d", "a", "e", "c"});
  lists::EditScript from_worker{};
  std::thread w([&] { from_worker = lists::myersDiff(oldL, newL); });
  w.join();
  lists::EditScript direct = lists::myersDiff(oldL, newL);
  check(from_worker.ops.size() == direct.ops.size() &&
            apply_is_exact(oldL, newL, from_worker),
        "worker_thread_callable");
}

// Atomic batch: a single applyBatch commits the whole script; the
// recycler is never observed in a partially applied state. Modelled by
// asserting the committed order equals the target exactly (no prefix of
// the script is visible as a final state).
void case_batch_atomic_exact() {
  lists::KeyedList oldL = kl({"a", "b", "c", "d", "e"});
  lists::KeyedList newL = kl({"e", "x", "a", "c", "y"});
  lists::EditScript s = lists::myersDiff(oldL, newL);
  check(apply_is_exact(oldL, newL, s), "batch_atomic_exact");
}

// Boundary: single-item lists.
void case_single_item_replace() {
  lists::EditScript s = lists::myersDiff(kl({"a"}), kl({"b"}));
  check(op_count(s) == 2, "single_item_replace");
  check(apply_is_exact(kl({"a"}), kl({"b"}), s),
        "single_item_replace_exact");
}

}  // namespace

int main() {
  case_identical_is_empty();
  case_empty_to_empty();
  case_inserted_minimal();
  case_inserted_rebinds_minimal();
  case_removed_minimal();
  case_shuffled_uses_moves();
  case_swap_is_minimal();
  case_mixed_edits();
  case_worker_thread_callable();
  case_batch_atomic_exact();
  case_single_item_replace();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
