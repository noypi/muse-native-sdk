// T1 contract tests — FR-LISTS-004 / AC-LISTS-018 (TC-LISTS-018).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services. Edit scripts are produced with the
// landed `lists::myersDiff` (no diffing reimplemented here).
// Binds to the T1 `Interface:` lines:
//   `ItemAnimScript BuildAnimScript(const EditScript& diff, const KeySet& visible);`
//   `bool IsRebound(ItemKey k, const ItemAnimScript& s)` (false for unchanged).
// Include path follows the engine convention:
//   #include "lists/item_animator.h" with engine/lists/include on the path.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace, the `KeySet`/`ItemAnimScript` shapes, or the op
// representation. This test proposes: namespace `lists`;
// `KeySet{std::vector<std::string> keys}` (visible keys); `AnimType::{
// Add, Remove, Move }` with `ItemAnimOp{AnimType type; std::string key;
// size_t from; size_t to}`; `ItemAnimScript{std::vector<ItemAnimOp> ops}`;
// `EditScript` consumed from `lists/myers_diff.h` (opaque input, the
// LISTS-001 Myers script); `ItemKey` from `lists/stable_key.h`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (diff-driven ops, visible-window
// filtering, unchanged never rebound).
//
// ASSUMPTION (visible filtering): only keys in `visible` produce anim ops;
// off-screen keys get no op and `IsRebound` is false for them. The AC does
// not pin this down; filtering to the realized window is the most
// defensible reading of "input is the Myers edit script + visible keys".
//
// ASSUMPTION (Change ops): the `EditScript` alphabet is Insert/Remove/Move
// only, so same-key content changes have no producer through this
// signature. No `Change` case is asserted here; see contract T1.md Gaps.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/item_animator.h"
#include "lists/myers_diff.h"
#include "lists/stable_key.h"

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

lists::KeyedList kl(size_t n, size_t skip_start = 0, size_t skip_count = 0) {
  lists::KeyedList l{};
  l.keys.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (i >= skip_start && i < skip_start + skip_count) continue;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    l.keys.emplace_back(buf);
  }
  return l;
}

lists::KeySet visible_all(const lists::KeyedList& l) {
  lists::KeySet v{};
  v.keys = l.keys;
  return v;
}

bool has_op(const lists::ItemAnimScript& s, lists::AnimType t,
            const std::string& key) {
  for (const auto& op : s.ops) {
    if (op.type == t && op.key == key) return true;
  }
  return false;
}

bool all_ops_visible(const lists::ItemAnimScript& s,
                     const lists::KeySet& v) {
  for (const auto& op : s.ops) {
    bool found = false;
    for (const auto& k : v.keys) {
      if (op.key == k) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}

// TC-LISTS-018 core: removing 100 of 10,000 items animates removals with
// zero rebinds of unchanged items.
void case_remove_100_of_10k() {
  lists::KeyedList oldL = kl(10000);
  lists::KeyedList newL = kl(10000, /*skip_start=*/5000, /*skip_count=*/100);
  lists::EditScript diff = lists::myersDiff(oldL, newL);
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, visible_all(newL));
  // Every removed key animates as a removal.
  bool all_removed = true;
  for (size_t i = 5000; i < 5100; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    if (!has_op(s, lists::AnimType::Remove, buf)) all_removed = false;
  }
  check(all_removed, "remove_100_animate_removals");
  // Unchanged items are not rebound: sample head, gap edges, tail.
  bool none_rebound = true;
  for (size_t i : {size_t(0), size_t(4999), size_t(5100), size_t(9999)}) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    if (lists::IsRebound(lists::ItemKey{buf}, s)) none_rebound = false;
  }
  check(none_rebound, "remove_100_unchanged_not_rebound");
}

// Reorder animates moves; moved items rebound, bystanders do not.
void case_move_animates() {
  lists::KeyedList oldL = kl(200);
  lists::KeyedList newL = oldL;
  // Move k00199 to the front.
  newL.keys.erase(newL.keys.begin() + 199);
  newL.keys.insert(newL.keys.begin(), "k00199");
  lists::EditScript diff = lists::myersDiff(oldL, newL);
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, visible_all(newL));
  check(has_op(s, lists::AnimType::Move, "k00199"), "move_animates");
  check(lists::IsRebound(lists::ItemKey{"k00199"}, s), "moved_is_rebound");
  check(!lists::IsRebound(lists::ItemKey{"k00050"}, s),
        "bystander_not_rebound");
}

// Insert animates adds.
void case_insert_animates_add() {
  lists::KeyedList oldL = kl(50);
  lists::KeyedList newL = oldL;
  newL.keys.insert(newL.keys.begin() + 25, "k-new");
  lists::EditScript diff = lists::myersDiff(oldL, newL);
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, visible_all(newL));
  check(has_op(s, lists::AnimType::Add, "k-new"), "insert_animates_add");
  check(lists::IsRebound(lists::ItemKey{"k-new"}, s), "added_is_rebound");
}

// Identical lists: empty anim script, nothing rebound.
void case_identical_is_empty() {
  lists::KeyedList oldL = kl(100);
  lists::EditScript diff = lists::myersDiff(oldL, oldL);
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, visible_all(oldL));
  check(s.ops.empty(), "identical_empty_script");
  check(!lists::IsRebound(lists::ItemKey{"k00000"}, s),
        "identical_nothing_rebound");
}

// Off-screen keys produce no ops even when the diff touches them.
void case_offscreen_excluded() {
  lists::KeyedList oldL = kl(1000);
  lists::KeyedList newL = kl(1000, /*skip_start=*/900, /*skip_count=*/10);
  lists::EditScript diff = lists::myersDiff(oldL, newL);
  // Visible window covers only the head of the list.
  lists::KeySet v{};
  for (size_t i = 0; i < 50; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    v.keys.emplace_back(buf);
  }
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, v);
  check(all_ops_visible(s, v), "offscreen_no_ops");
  check(!lists::IsRebound(lists::ItemKey{"k00905"}, s),
        "offscreen_removed_not_rebound");
}

// Boundary: empty visible set yields an empty script.
void case_empty_visible() {
  lists::KeyedList oldL = kl(10);
  lists::KeyedList newL = kl(10, /*skip_start=*/2, /*skip_count=*/3);
  lists::EditScript diff = lists::myersDiff(oldL, newL);
  lists::KeySet v{};
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, v);
  check(s.ops.empty(), "empty_visible_empty_script");
}

// Boundary: empty diff yields an empty script.
void case_empty_diff() {
  lists::EditScript diff{};
  lists::KeySet v{};
  v.keys = {"k00000"};
  lists::ItemAnimScript s = lists::BuildAnimScript(diff, v);
  check(s.ops.empty(), "empty_diff_empty_script");
}

}  // namespace

int main() {
  case_remove_100_of_10k();
  case_move_animates();
  case_insert_animates_add();
  case_identical_is_empty();
  case_offscreen_excluded();
  case_empty_visible();
  case_empty_diff();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
