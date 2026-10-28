#include "lists/position_persist.h"

namespace lists {

Snapshot savePos(DestId d, float offsetPx, int index, ItemKey anchor,
                 float anchorFrac) {
  Snapshot s;
  s.dest = d;
  s.offsetPx = offsetPx;
  s.index = index;
  s.anchor = anchor;
  s.anchorFrac = anchorFrac;
  return s;
}

Restored restorePos(DestId d, const Snapshot& s, const KeyedList& data) {
  (void)d;
  Restored r;
  r.velocityPxS = 0.0f;
  if (data.keys.empty()) {
    r.index = 0;
    r.offsetPx = 0.0f;
    return r;
  }
  r.offsetPx = s.offsetPx < 0.0f ? 0.0f : s.offsetPx;
  // Anchor still present: restore at its current position.
  for (size_t i = 0; i < data.keys.size(); ++i) {
    if (data.keys[i] == s.anchor.key) {
      r.index = static_cast<int>(i);
      return r;
    }
  }
  // Anchor gone: nearest surviving index is the saved index clamped to
  // [0, size-1] (positions are contiguous, so clamping is the closest
  // position; ties toward the lower index fall out of the lower clamp).
  const int last = static_cast<int>(data.keys.size()) - 1;
  int index = s.index;
  if (index < 0) index = 0;
  if (index > last) index = last;
  r.index = index;
  return r;
}

}  // namespace lists
