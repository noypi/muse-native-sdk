// T4 contract tests — FR-INPUT-008 AC-INPUT-015 / TC-INPUT-015.
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the plan T4 `Interface:` line (reproduced verbatim in unit-tests/T4.md).
// Include path follows the engine/runtime convention:
//   #include "input/focus_restore.h" with engine/input/include on the path.
//
// ASSUMPTION (names/types): the plan Interface line names `FocusRestore`,
// `FocusSnapshot`, `FocusTree&`, `Status` and the kTargetGone + initial-focus
// fallback behavior, but settles no C++ spellings beyond that (namespace,
// FocusSnapshot fields, static-vs-instance methods, Status enumerants beyond
// kTargetGone, what "initial focus" is). This test assumes:
//   - `input::FocusRestore` class with `snapshot(FocusTree&)` returning
//     `input::FocusSnapshot` by value and `restore(FocusTree&,
//     FocusSnapshot)` returning `input::Status` (called here via an
//     instance, which also compiles if the coder declares them static);
//   - `input::FocusSnapshot` is a struct with a `NodeId node` member
//     (`-1` = empty);
//   - `input::Status` has at least `kOk` and `kTargetGone`;
//   - screen initial-focus policy = head of the current order (first
//     element passed to setOrder), observed via `exposeToA11y`.
// Rename-safe reconciliation: if the coder names them differently, update
// these call sites — not the expected behavior (logical-NodeId snapshot,
// restore lands on the same logical node for all three triggers,
// unknown node -> kTargetGone + initial-focus fallback).
#include <cstdio>
#include <vector>

#include "input/focus_restore.h"
#include "input/focus_tree.h"

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

input::NodeId focusedId(const input::FocusTree& t) {
  // NOLINTNEXTLINE: test helper reads a11y mirror; FocusTree exposes no getter.
  input::FocusTree& m = const_cast<input::FocusTree&>(t);
  for (const auto& n : m.exposeToA11y()) {
    if (n.focused) return n.id;
  }
  return -1;
}

// Snapshot is keyed to the logical NodeId, not a pointer/object identity.
void case_snapshot_captures_logical_node() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  (void)t.next(1);  // focus should now be 2.
  input::FocusRestore r;
  input::FocusSnapshot s = r.snapshot(t);
  check(s.node == 2, "restore_snapshot_captures_focused_node");
}

// TC-INPUT-015 trigger 1: nav-pop — destroy tree, rebuild same logical
// nodes, restore lands on the same logical node.
void case_restore_after_nav_pop_lands_same_node() {
  input::FocusRestore r;
  input::FocusSnapshot s;
  {
    input::FocusTree t;
    (void)t.setOrder({1, 2, 3});
    (void)t.next(1);  // focus 2.
    s = r.snapshot(t);
  }  // t destroyed (popped).
  input::FocusTree rebuilt;
  (void)rebuilt.setOrder({1, 2, 3});
  check(r.restore(rebuilt, s) == input::Status::kOk, "restore_nav_pop_ok");
  check(focusedId(rebuilt) == 2, "restore_nav_pop_same_logical_node");
}

// TC-INPUT-015 trigger 2: process death — snapshot outlives the tree object;
// a brand-new tree with the same logical nodes restores to the same node.
void case_restore_after_process_death_lands_same_node() {
  input::FocusRestore r;
  input::FocusSnapshot s;
  {
    input::FocusTree t;
    (void)t.setOrder({10, 20, 30});
    (void)t.next(20);  // focus 30.
    s = r.snapshot(t);
    check(s.node == 30, "restore_death_snapshot_holds_node");
  }  // process dies here: t is gone, only s survives.
  input::FocusTree fresh;
  (void)fresh.setOrder({10, 20, 30});
  check(r.restore(fresh, s) == input::Status::kOk, "restore_death_ok");
  check(focusedId(fresh) == 30, "restore_death_same_logical_node");
}

// TC-INPUT-015 trigger 3: config change with the same path — same logical
// nodes re-created, restore lands on the same logical node.
void case_restore_after_config_change_lands_same_node() {
  input::FocusRestore r;
  input::FocusTree t;
  (void)t.setOrder({5, 6, 7});
  (void)t.next(5);  // focus 6.
  input::FocusSnapshot s = r.snapshot(t);
  input::FocusTree recreated;  // config change re-creates the tree.
  (void)recreated.setOrder({5, 6, 7});
  check(r.restore(recreated, s) == input::Status::kOk, "restore_config_ok");
  check(focusedId(recreated) == 6, "restore_config_same_logical_node");
}

// Restore must not reorder the tree: order is identical after restore.
void case_restore_preserves_order() {
  input::FocusRestore r;
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  (void)t.next(1);
  input::FocusSnapshot s = r.snapshot(t);
  input::FocusTree rebuilt;
  (void)rebuilt.setOrder({1, 2, 3});
  (void)r.restore(rebuilt, s);
  std::vector<input::A11yNode> nodes = rebuilt.exposeToA11y();
  bool ok = nodes.size() == 3u && nodes[0].id == 1 && nodes[1].id == 2 &&
            nodes[2].id == 3;
  check(ok, "restore_preserves_order");
}

// Error: unknown NodeId in snapshot returns kTargetGone and falls back to
// the screen initial-focus policy (assumed: head of order).
void case_unknown_snapshot_node_returns_target_gone() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  input::FocusRestore r;
  input::FocusSnapshot s;
  s.node = 999;
  check(r.restore(t, s) == input::Status::kTargetGone,
        "restore_unknown_node_target_gone");
  check(focusedId(t) == 1, "restore_unknown_node_initial_focus_fallback");
}

// Error: snapshotted node removed from the rebuilt tree -> kTargetGone +
// initial-focus fallback.
void case_removed_node_returns_target_gone() {
  input::FocusRestore r;
  input::FocusSnapshot s;
  {
    input::FocusTree t;
    (void)t.setOrder({1, 2, 3});
    (void)t.next(2);  // focus 3.
    s = r.snapshot(t);
  }
  input::FocusTree rebuilt;
  (void)rebuilt.setOrder({1, 2});  // node 3 gone.
  check(r.restore(rebuilt, s) == input::Status::kTargetGone,
        "restore_removed_node_target_gone");
  check(focusedId(rebuilt) == 1, "restore_removed_node_initial_focus");
}

// Boundary: restoring into an empty tree cannot land anywhere ->
// kTargetGone (ASSUMPTION: empty tree has no initial focus; the Status half
// is pinned, focus observation is vacuous).
void case_restore_into_empty_tree_returns_target_gone() {
  input::FocusRestore r;
  input::FocusSnapshot s;
  s.node = 1;
  input::FocusTree empty;
  check(r.restore(empty, s) == input::Status::kTargetGone,
        "restore_empty_tree_target_gone");
}

}  // namespace

int main() {
  case_snapshot_captures_logical_node();
  case_restore_after_nav_pop_lands_same_node();
  case_restore_after_process_death_lands_same_node();
  case_restore_after_config_change_lands_same_node();
  case_restore_preserves_order();
  case_unknown_snapshot_node_returns_target_gone();
  case_removed_node_returns_target_gone();
  case_restore_into_empty_tree_returns_target_gone();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
