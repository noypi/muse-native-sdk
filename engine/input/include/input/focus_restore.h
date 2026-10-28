#pragma once

#include "input/focus_tree.h"

namespace input {

// Snapshot of focus keyed to the logical NodeId (not a pointer/object
// identity). `node == -1` means empty (no focused node observed).
struct FocusSnapshot {
  NodeId node = -1;
};

// Seam owned by nav/lifecycle callers (NAV-RESTORE FR-NAV-009, LIFE-SAVE
// FR-LIFECYCLE-005 consume-only: not implemented here). Callers simulate the
// three TC-INPUT-015 triggers by destroying + rebuilding the tree in-process
// and calling restore on the rebuilt tree with the surviving snapshot.
class FocusRestore {
 public:
  FocusRestore() = default;

  // Captures the currently focused logical node. Never reorders the tree.
  FocusSnapshot snapshot(FocusTree& tree);

  // Moves focus to the snapshotted logical node. Returns Status::kOk when the
  // node exists in the tree; otherwise returns Status::kTargetGone and falls
  // back to the screen initial-focus policy (head of the current order).
  // Never reorders the tree. Requires Status::kTargetGone (plan T4
  // Interface line; enumerant lives in input/focus_tree.h — out of T4 claim).
  Status restore(FocusTree& tree, FocusSnapshot snapshot);
};

}  // namespace input
