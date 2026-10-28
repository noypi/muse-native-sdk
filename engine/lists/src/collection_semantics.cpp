#include "lists/collection_semantics.h"

namespace lists {

namespace {

// Upper bound of the leading index: the last window start. Never negative:
// an empty or fully-realized collection pins the window at 0.
int MaxLeading(const CollectionNode& n) {
  const int max = n.totalCount - n.realizedCount;
  return max > 0 ? max : 0;
}

// Clamps a leading index into the valid window range.
int ClampLeading(const CollectionNode& n, int leading) {
  if (leading < 0) return 0;
  const int max = MaxLeading(n);
  return leading > max ? max : leading;
}

}  // namespace

A11yActions GetScrollActions(const CollectionNode& n) {
  A11yActions a;
  // Windowed realization never clears the scrollable flag.
  a.scrollable = true;
  const int leading = ClampLeading(n, n.leadingIndex);
  a.canScrollBackward = leading > 0;
  a.canScrollForward = leading + n.realizedCount < n.totalCount;
  return a;
}

ScrollResult PerformScrollAction(CollectionNode& n, ScrollDir d) {
  // Pin a drifted leading index before stepping so the window stays valid.
  n.leadingIndex = ClampLeading(n, n.leadingIndex);
  const int max = MaxLeading(n);
  // A degenerate window (empty collection, zero realized span, or a window
  // covering the whole collection) cannot move.
  if (n.realizedCount <= 0 || n.totalCount <= 0 || max <= 0) {
    return ScrollResult{n.leadingIndex, false};
  }
  int next = n.leadingIndex;
  if (d == ScrollDir::Forward) {
    next = n.leadingIndex + n.realizedCount;
    if (next > max) next = max;
  } else {
    next = n.leadingIndex - n.realizedCount;
    if (next < 0) next = 0;
  }
  if (next == n.leadingIndex) {
    return ScrollResult{n.leadingIndex, false};
  }
  n.leadingIndex = next;
  return ScrollResult{next, true};
}

}  // namespace lists
