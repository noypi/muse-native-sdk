#include "input/focus_restore.h"

namespace input {

namespace {

// Returns the currently focused logical node, or -1 when none is focused
// (e.g. empty tree). Reads the a11y mirror; never mutates the tree.
NodeId FocusedId(FocusTree& tree) {
  for (const A11yNode& n : tree.exposeToA11y()) {
    if (n.focused) return n.id;
  }
  return -1;
}

}  // namespace

FocusSnapshot FocusRestore::snapshot(FocusTree& tree) {
  FocusSnapshot out;
  out.node = FocusedId(tree);
  return out;
}

Status FocusRestore::restore(FocusTree& tree, FocusSnapshot snapshot) {
  const std::vector<A11yNode> nodes = tree.exposeToA11y();
  if (nodes.empty()) {
    return Status::kTargetGone;
  }
  bool present = false;
  for (const A11yNode& n : nodes) {
    if (n.id == snapshot.node) {
      present = true;
      break;
    }
  }
  const NodeId head = nodes.front().id;
  const NodeId target = present ? snapshot.node : head;
  // Drive the ring until focus lands on target. Bounded by order size since
  // next() wraps deterministically; never reorders the tree.
  NodeId current = FocusedId(tree);
  for (std::size_t i = 0; i < nodes.size() + 1; ++i) {
    if (current == target) break;
    current = tree.next(current);
  }
  if (!present) {
    return Status::kTargetGone;
  }
  return Status::kOk;
}

}  // namespace input
