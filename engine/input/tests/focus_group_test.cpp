// T2 contract tests — FR-INPUT-008 AC-INPUT-014 / TC-INPUT-014 (focus groups).
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the plan T2 `Interface:` line (reproduced verbatim in unit-tests/T2.md).
// Include path follows the engine/runtime convention:
//   #include "input/focus_tree.h" with engine/input/include on the path.
//
// ASSUMPTION (group wiring): the Interface line names enterGroup/exitGroup but
// does not settle how nodes join a group (constructor mapping, addToGroup, or
// order-partition). This test assumes the minimal wire-up the coder is most
// likely to provide: `setOrder` establishes the ring and group membership is
// established by entering a group id that traps subsequent next/prev within
// the members the tree associates with that id. Where membership needs an
// explicit call, the assumed spelling is
// `assignToGroup(NodeId, GroupId): Status` — marked ASSUMPTION; if the coder
// wires membership differently, update the wire-up calls, not the trapping
// expectations (trapped group never leaks; exit restores outer ring;
// unknown ids kNotFound with focus unchanged; a11y mirrors order/focus).
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

bool contains(const std::vector<input::A11yNode>& nodes, input::NodeId id) {
  for (const auto& n : nodes) {
    if (n.id == id) return true;
  }
  return false;
}

input::NodeId focused_id(const std::vector<input::A11yNode>& nodes) {
  for (const auto& n : nodes) {
    if (n.focused) return n.id;
  }
  return -1;
}

// TC-INPUT-014: entering a group traps traversal inside it.
void case_enter_group_traps_next() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  // ASSUMPTION wire-up: nodes 2,3 belong to group 100.
  (void)t.assignToGroup(2, 100);
  (void)t.assignToGroup(3, 100);
  check(t.enterGroup(100) == input::Status::kOk, "group_enter_ok");
  // Walk several steps from inside the group; focus must never leave {2,3}.
  input::NodeId cur = 2;
  bool leaked = false;
  for (int i = 0; i < 6; ++i) {
    cur = t.next(cur);
    if (cur != 2 && cur != 3) leaked = true;
  }
  check(!leaked, "group_trapped_next_never_leaks");
}

void case_enter_group_traps_prev() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  // ASSUMPTION wire-up: nodes 2,3 belong to group 100.
  (void)t.assignToGroup(2, 100);
  (void)t.assignToGroup(3, 100);
  (void)t.enterGroup(100);
  input::NodeId cur = 3;
  bool leaked = false;
  for (int i = 0; i < 6; ++i) {
    cur = t.prev(cur);
    if (cur != 2 && cur != 3) leaked = true;
  }
  check(!leaked, "group_trapped_prev_never_leaks");
}

// Trapped ring still wraps within the group.
void case_trapped_group_wraps_internally() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  // ASSUMPTION wire-up: nodes 2,3 belong to group 100.
  (void)t.assignToGroup(2, 100);
  (void)t.assignToGroup(3, 100);
  (void)t.enterGroup(100);
  check(t.next(3) == 2, "group_wraps_next_inside");
  check(t.prev(2) == 3, "group_wraps_prev_inside");
}

// exitGroup restores the outer ring.
void case_exit_group_restores_outer_ring() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  // ASSUMPTION wire-up: nodes 2,3 belong to group 100.
  (void)t.assignToGroup(2, 100);
  (void)t.assignToGroup(3, 100);
  (void)t.enterGroup(100);
  check(t.exitGroup() == input::Status::kOk, "group_exit_ok");
  check(t.next(4) == 1, "group_exit_outer_wrap");
  check(t.next(1) == 2, "group_exit_outer_step");
}

// Error: entering an unknown group returns kNotFound, focus unchanged.
void case_enter_unknown_group_not_found_focus_unchanged() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  input::NodeId before = focused_id(t.exposeToA11y());
  check(t.enterGroup(9999) == input::Status::kNotFound,
        "group_enter_unknown_not_found");
  check(focused_id(t.exposeToA11y()) == before,
        "group_enter_unknown_focus_unchanged");
}

// exposeToA11y mirrors order/focus while trapped.
void case_a11y_mirrors_trapped_group() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  // ASSUMPTION wire-up: nodes 2,3 belong to group 100.
  (void)t.assignToGroup(2, 100);
  (void)t.assignToGroup(3, 100);
  (void)t.enterGroup(100);
  (void)t.next(2);
  std::vector<input::A11yNode> nodes = t.exposeToA11y();
  check(contains(nodes, 2) && contains(nodes, 3),
        "group_a11y_contains_members");
  int focusedCount = 0;
  for (const auto& n : nodes) {
    if (n.focused) ++focusedCount;
  }
  check(focusedCount == 1, "group_a11y_single_focus_while_trapped");
}

}  // namespace

int main() {
  case_enter_group_traps_next();
  case_enter_group_traps_prev();
  case_trapped_group_wraps_internally();
  case_exit_group_restores_outer_ring();
  case_enter_unknown_group_not_found_focus_unchanged();
  case_a11y_mirrors_trapped_group();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
