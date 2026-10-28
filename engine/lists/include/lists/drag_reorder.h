#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lists {

// Owner of an in-flight drag gesture (D1 precedence).
enum class DragOwner { VerticalList, HorizontalParent };

// Verdict of a drag-claim arbitration.
struct DragVerdict {
  DragOwner owner = DragOwner::HorizontalParent;
  std::string reason;
};

// Modelled view of the gesture-arena claim state, mirroring
// `ArenaAction::{claimed, winnerId}` in `engine/input/include/gesture_arena.h`
// (read-only boundary; the arena itself is never included or linked here).
struct ArenaClaim {
  bool claimed = false;
  int winnerId = -1;
};

// Reorder session state: the current key order, the index of the dragged
// item in `order`, and the count of model updates applied so far.
struct ReorderState {
  std::vector<std::string> order;
  size_t draggedFrom = 0;
  int updates = 0;
};

// Result of committing a drop.
struct DropResult {
  bool ok = false;
  int index = 0;
  int updatesApplied = 0;
};

// Arbitrates a drag claim per D1: the vertical list owns the gesture iff a
// long-press initiated the session, vertical displacement strictly dominates
// horizontal displacement, and the arena is unclaimed. Ties, zero
// displacement, missing long-press, horizontal dominance, and a foreign arena
// claim all yield to the horizontal parent.
DragVerdict ResolveDragClaim(float dx, float dy, bool longPress,
                             ArenaClaim arena);

// Commits a drop by moving the item at `s.draggedFrom` to `toIndex`,
// applying exactly one model update (`s.updates += 1`). Out-of-range
// `toIndex` values clamp into the valid insertion range; dropping at the
// dragged position is a content no-op that still accounts the single update.
DropResult CommitDrop(ReorderState& s, int toIndex);

}  // namespace lists
