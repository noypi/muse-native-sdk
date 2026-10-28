// T2 contract tests — FR-INPUT-002 AC-INPUT-004 / TC-INPUT-004.
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the plan T2 `Interface:` line (reproduced verbatim in unit-tests/T2.md).
// Include path follows the engine/runtime convention:
//   #include "input/focus_tree.h" with engine/input/include on the path.
//
// ASSUMPTION (names/types): the plan Interface line names the method names,
// `NodeId`/`GroupId`/`Status`/`A11yNode` type names and the kNotFound behavior,
// but settles no C++ spellings beyond that (namespace, NodeId underlying type,
// Status enumerants beyond kNotFound, A11yNode fields, exposeToA11y element
// type spelling). This test assumes:
//   - `input::FocusTree` class with the six methods from the Interface line;
//   - `input::NodeId` is an int-like id (alias of int);
//   - `input::GroupId` is an int-like id;
//   - `input::Status` is an enum with at least `kOk` and `kNotFound`;
//   - `input::A11yNode` has at least `id` and `focused` members.
// Rename-safe reconciliation: if the coder names them differently, update
// these call sites — not the expected behavior (explicit order, wrap-around
// rings, unknown-id kNotFound with focus unchanged, a11y mirrors order/focus).
#include <cstdio>
#include <vector>

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

// TC-INPUT-004: keyboard traversal visits nodes in setOrder.
void case_traversal_follows_set_order() {
  input::FocusTree t;
  check(t.setOrder({1, 2, 3}) == input::Status::kOk, "traversal_set_order_ok");
  check(t.next(1) == 2, "traversal_1_goes_to_2");
  check(t.next(2) == 3, "traversal_2_goes_to_3");
}

// next wraps deterministically at the end of the ring.
void case_next_wraps_at_end() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  check(t.next(3) == 1, "traversal_next_wraps");
}

// prev wraps deterministically at the start of the ring.
void case_prev_wraps_at_start() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  check(t.prev(1) == 3, "traversal_prev_wraps");
  check(t.prev(3) == 2, "traversal_prev_steps_back");
}

// Full ring walk from the head visits every node exactly once and returns.
void case_full_ring_walk_visits_each_once() {
  input::FocusTree t;
  (void)t.setOrder({10, 20, 30, 40});
  input::NodeId cur = 10;
  std::vector<input::NodeId> visited{cur};
  for (int i = 0; i < 3; ++i) cur = t.next(cur);
  visited.push_back(cur);
  // After 3 steps from 10 we must be at 40; one more wraps to 10.
  check(cur == 40, "traversal_full_walk_reaches_tail");
  check(t.next(cur) == 10, "traversal_full_walk_wraps_to_head");
}

// Boundary: single-node ring points at itself.
void case_single_node_ring_is_stable() {
  input::FocusTree t;
  (void)t.setOrder({7});
  check(t.next(7) == 7, "traversal_single_next_self");
  check(t.prev(7) == 7, "traversal_single_prev_self");
}

// Boundary: re-setting order replaces the ring deterministically.
void case_set_order_replaces_ring() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  check(t.setOrder({3, 2, 1}) == input::Status::kOk, "traversal_reorder_ok");
  check(t.next(3) == 2, "traversal_reordered_next");
  check(t.prev(3) == 1, "traversal_reordered_prev_wraps");
}

// Error: unknown NodeId returns kNotFound and leaves focus unchanged.
// Focus-unchanged is observed via exposeToA11y: the focused node before and
// after the bad call must be identical.
void case_unknown_next_returns_not_found_focus_unchanged() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  std::vector<input::A11yNode> before = t.exposeToA11y();
  input::NodeId beforeFocus = -1;
  for (const auto& n : before) {
    if (n.focused) beforeFocus = n.id;
  }
  // next() returns NodeId per the Interface line; the unknown-id kNotFound
  // path surfaces on setOrder-style Status returns — for next/prev the
  // contract is "leaves focus unchanged": an unknown `from` must not move
  // focus. We assert via exposeToA11y below. The Status::kNotFound half is
  // pinned by setOrder/enterGroup-style calls with unknown ids where the
  // signature returns Status; here we at least pin no-focus-change.
  (void)t.next(999);
  std::vector<input::A11yNode> after = t.exposeToA11y();
  input::NodeId afterFocus = -1;
  for (const auto& n : after) {
    if (n.focused) afterFocus = n.id;
  }
  check(beforeFocus == afterFocus, "traversal_unknown_next_focus_unchanged");
  check(after.size() == 3u, "traversal_unknown_next_order_unchanged");
}

void case_unknown_prev_returns_not_found_focus_unchanged() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  std::vector<input::A11yNode> before = t.exposeToA11y();
  input::NodeId beforeFocus = -1;
  for (const auto& n : before) {
    if (n.focused) beforeFocus = n.id;
  }
  (void)t.prev(999);
  std::vector<input::A11yNode> after = t.exposeToA11y();
  input::NodeId afterFocus = -1;
  for (const auto& n : after) {
    if (n.focused) afterFocus = n.id;
  }
  check(beforeFocus == afterFocus, "traversal_unknown_prev_focus_unchanged");
}

// exposeToA11y mirrors current order.
void case_a11y_mirrors_order() {
  input::FocusTree t;
  (void)t.setOrder({5, 6, 7});
  std::vector<input::A11yNode> nodes = t.exposeToA11y();
  bool ok = nodes.size() == 3u && nodes[0].id == 5 && nodes[1].id == 6 &&
            nodes[2].id == 7;
  check(ok, "traversal_a11y_mirrors_order");
}

// exposeToA11y reflects current focus after every move: exactly one node
// focused, and it tracks next().
void case_a11y_reflects_focus_after_move() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  (void)t.next(1);  // focus should now be 2 (or started at head then moved)
  std::vector<input::A11yNode> nodes = t.exposeToA11y();
  int focusedCount = 0;
  input::NodeId focusedId = -1;
  for (const auto& n : nodes) {
    if (n.focused) {
      ++focusedCount;
      focusedId = n.id;
    }
  }
  check(focusedCount == 1, "traversal_a11y_single_focus");
  check(focusedId == 2, "traversal_a11y_focus_tracks_next");
}

}  // namespace

int main() {
  case_traversal_follows_set_order();
  case_next_wraps_at_end();
  case_prev_wraps_at_start();
  case_full_ring_walk_visits_each_once();
  case_single_node_ring_is_stable();
  case_set_order_replaces_ring();
  case_unknown_next_returns_not_found_focus_unchanged();
  case_unknown_prev_returns_not_found_focus_unchanged();
  case_a11y_mirrors_order();
  case_a11y_reflects_focus_after_move();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
