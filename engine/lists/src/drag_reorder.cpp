#include "lists/drag_reorder.h"

#include <cmath>

namespace lists {

DragVerdict ResolveDragClaim(float dx, float dy, bool longPress,
                             ArenaClaim arena) {
  if (arena.claimed) {
    return {DragOwner::HorizontalParent, "arena already claimed"};
  }
  if (!longPress) {
    return {DragOwner::HorizontalParent, "no long-press: no reorder session"};
  }
  const float ax = std::fabs(dx);
  const float ay = std::fabs(dy);
  if (ay == 0.0f && ax == 0.0f) {
    return {DragOwner::HorizontalParent, "zero displacement claims nothing"};
  }
  if (ay > ax) {
    return {DragOwner::VerticalList, "vertical dominant with long-press"};
  }
  return {DragOwner::HorizontalParent,
          "horizontal dominant or tie yields to parent"};
}

DropResult CommitDrop(ReorderState& s, int toIndex) {
  if (s.order.empty() || s.draggedFrom >= s.order.size()) {
    return {false, 0, 0};
  }
  std::string dragged = s.order[s.draggedFrom];
  s.order.erase(s.order.begin() +
                static_cast<std::vector<std::string>::difference_type>(
                    s.draggedFrom));
  int postSize = static_cast<int>(s.order.size());
  int clamped = toIndex;
  if (clamped < 0) {
    clamped = 0;
  } else if (clamped > postSize) {
    clamped = postSize;
  }
  s.order.insert(
      s.order.begin() +
          static_cast<std::vector<std::string>::difference_type>(clamped),
      std::move(dragged));
  s.draggedFrom = static_cast<size_t>(clamped);
  s.updates += 1;
  return {true, clamped, 1};
}

}  // namespace lists
