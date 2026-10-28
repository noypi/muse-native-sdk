// T5 contract tests — FR-INPUT-008 AC-INPUT-016 / TC-INPUT-016.
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the plan T5 `Interface:` line (reproduced verbatim in unit-tests/T5.md).
// Include path follows the engine/runtime convention:
//   #include "input/focus_tree.h" with engine/input/include on the path.
//
// ASSUMPTION (names/types): the plan Interface line names
// `FocusTree::rebuild(Subtree s): Status` and the kOrderChanged behavior, but
// settles no C++ spellings beyond that (Subtree shape, Status enumerants
// beyond kOrderChanged, how ring-flash is observed). This test assumes:
//   - `input::Subtree` is a struct with a `nodes` member
//     (`std::vector<NodeId>` holding the rebuilt subtree's logical order);
//   - `input::Status` has at least `kOk` and `kOrderChanged`;
//   - ring flash is observed via `exposeToA11y`: when the focus target is
//     unchanged across a rebuild, the focused node id is identical
//     pre/post rebuild (flag false == no focus change).
// Rename-safe reconciliation: if the coder wires Subtree membership
// differently, update the makeSubtree helper — not the expected behavior
// (prior logical order + focus preserved; different order -> kOrderChanged;
// mixed-form order identical pre/post; no ring flash when target unchanged).
#include <cstdio>
#include <utility>
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

// ASSUMPTION: Subtree wire-up — struct with a `nodes` member.
input::Subtree makeSubtree(std::vector<input::NodeId> nodes) {
  input::Subtree s;
  s.nodes = std::move(nodes);
  return s;
}

input::NodeId focusedId(input::FocusTree& t) {
  for (const auto& n : t.exposeToA11y()) {
    if (n.focused) return n.id;
  }
  return -1;
}

std::vector<input::NodeId> orderOf(input::FocusTree& t) {
  std::vector<input::NodeId> out;
  for (const auto& n : t.exposeToA11y()) out.push_back(n.id);
  return out;
}

// Destroy/rebuild of the focused subtree preserves order + focus to the
// same logical node.
void case_rebuild_preserves_order_and_focus() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  (void)t.next(2);  // focus 3.
  check(t.rebuild(makeSubtree({1, 2, 3, 4})) == input::Status::kOk,
        "rebuild_same_order_ok");
  check(orderOf(t) == std::vector<input::NodeId>({1, 2, 3, 4}),
        "rebuild_order_preserved");
  check(focusedId(t) == 3, "rebuild_focus_same_logical_node");
}

// TC-INPUT-016: mixed form (text fields, buttons, toggles) has one fixed
// traversal order across rebuilds.
void case_mixed_form_order_identical_pre_post_rebuild() {
  // 101,102 = text fields; 201,202 = buttons; 301,302 = toggles.
  const std::vector<input::NodeId> kMixedForm = {101, 102, 201, 202, 301, 302};
  input::FocusTree t;
  (void)t.setOrder(kMixedForm);
  const std::vector<input::NodeId> pre = orderOf(t);
  check(t.rebuild(makeSubtree(kMixedForm)) == input::Status::kOk,
        "rebuild_mixed_form_ok");
  const std::vector<input::NodeId> post = orderOf(t);
  check(pre == post, "rebuild_mixed_form_order_identical");
  check(post == kMixedForm, "rebuild_mixed_form_order_is_canonical");
}

// Traversal walk is identical pre/post rebuild: next-chain from head visits
// the same sequence.
void case_traversal_walk_identical_pre_post_rebuild() {
  input::FocusTree t;
  (void)t.setOrder({101, 102, 201, 202, 301, 302});
  std::vector<input::NodeId> preWalk;
  input::NodeId cur = 101;
  for (int i = 0; i < 6; ++i) {
    preWalk.push_back(cur);
    cur = t.next(cur);
  }
  (void)t.rebuild(makeSubtree({101, 102, 201, 202, 301, 302}));
  std::vector<input::NodeId> postWalk;
  cur = 101;
  for (int i = 0; i < 6; ++i) {
    postWalk.push_back(cur);
    cur = t.next(cur);
  }
  check(preWalk == postWalk, "rebuild_traversal_walk_identical");
  check(t.next(302) == 101, "rebuild_ring_wraps_after_rebuild");
}

// Error: caller passes a different order -> kOrderChanged.
void case_rebuild_different_order_returns_order_changed() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  check(t.rebuild(makeSubtree({3, 2, 1})) == input::Status::kOrderChanged,
        "rebuild_different_order_order_changed");
}

// Boundary: rebuilt subtree missing the focused node keeps the surviving
// order; focus must move somewhere valid (observed: exactly one focused
// node remains). ASSUMPTION: fallback target is not pinned beyond validity.
void case_rebuild_missing_focused_node_keeps_valid_focus() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3});
  (void)t.next(2);  // focus 3.
  (void)t.rebuild(makeSubtree({1, 2}));
  int focusedCount = 0;
  for (const auto& n : t.exposeToA11y()) {
    if (n.focused) ++focusedCount;
  }
  check(focusedCount == 1, "rebuild_missing_node_single_focus");
}

// Ring-flash flag is false when the focus target is unchanged: the focused
// node id is identical immediately pre/post rebuild.
void case_no_ring_flash_when_focus_target_unchanged() {
  input::FocusTree t;
  (void)t.setOrder({1, 2, 3, 4});
  (void)t.next(1);  // focus 2.
  const input::NodeId preFocus = focusedId(t);
  (void)t.rebuild(makeSubtree({1, 2, 3, 4}));
  const input::NodeId postFocus = focusedId(t);
  check(preFocus == 2, "rebuild_flash_precondition_focus_2");
  check(postFocus == preFocus, "rebuild_no_ring_flash_focus_unchanged");
}

}  // namespace

int main() {
  case_rebuild_preserves_order_and_focus();
  case_mixed_form_order_identical_pre_post_rebuild();
  case_traversal_walk_identical_pre_post_rebuild();
  case_rebuild_different_order_returns_order_changed();
  case_rebuild_missing_focused_node_keeps_valid_focus();
  case_no_ring_flash_when_focus_target_unchanged();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
