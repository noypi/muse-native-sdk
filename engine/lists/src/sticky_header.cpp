#include "lists/sticky_header.h"

namespace lists {

StickyFrame UpdateSticky(const StickyState& s, float scrollOffset) {
  StickyFrame f;
  if (s.headers.empty()) return f;
  // Pin the last header at or above the offset; clamp pre-first offsets to
  // the first header so extreme (negative) offsets still pin deterministically.
  size_t pinned = 0;
  for (size_t i = 0; i < s.headers.size(); ++i) {
    if (s.headers[i].offset <= scrollOffset) pinned = i;
  }
  const HeaderGeo& cur = s.headers[pinned];
  f.pinnedId = cur.id;
  // Push-out touches at most the immediate successor: when its top edge (in
  // viewport coordinates) slides under the pinned header's bottom edge, the
  // pinned header rides up by the overlap amount.
  if (pinned + 1 < s.headers.size()) {
    const HeaderGeo& next = s.headers[pinned + 1];
    float nextTop = next.offset - scrollOffset;
    if (nextTop < cur.height) {
      f.pushedById = next.id;
      f.yAdjust = nextTop - cur.height;
    }
  }
  return f;
}

}  // namespace lists
