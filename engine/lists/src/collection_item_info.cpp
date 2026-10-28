#include "lists/collection_item_info.h"

namespace lists {
namespace {

// A zero spanCount still lays out: clamp to a single column (same rule as
// BuildLayout in collection_contract.cpp).
int EffectiveSpanCount(const CollectionNode& n) {
  return n.spanCount < 1 ? 1 : n.spanCount;
}

// Per-item grid span from the parallel `spans` array; missing entries
// default to 1 and everything clamps to [1, spanCount].
int ItemSpan(const CollectionNode& n, size_t i, int spanCount) {
  int span = 1;
  if (i < n.spans.size()) span = n.spans[i];
  if (span < 1) span = 1;
  if (span > spanCount) span = spanCount;
  return span;
}

}  // namespace

CollectionItemInfo GetItemInfo(const CollectionNode& n, ItemKey k) {
  CollectionItemInfo out;
  out.count = static_cast<int>(n.items.size());
  int index = -1;
  for (size_t i = 0; i < n.items.size(); ++i) {
    if (n.items[i] == k) {
      index = static_cast<int>(i);
      break;
    }
  }
  out.index = index;
  if (index < 0) return out;  // Row stays -1, column 0, spans 1; count intact.
  if (n.mode == CollectionMode::List) {
    out.row = index;
    out.column = 0;
    out.rowSpan = 1;
    out.colSpan = 1;
    return out;
  }
  // Grid modes: replay the T6 left-to-right packing so row/column match the
  // realized frames; span items keep their multi-cell extents.
  const int spanCount = EffectiveSpanCount(n);
  int col = 0;
  int row = 0;
  for (size_t i = 0; i < n.items.size(); ++i) {
    const int span = ItemSpan(n, i, spanCount);
    if (col + span > spanCount) {
      col = 0;
      ++row;
    }
    if (static_cast<int>(i) == index) {
      out.row = row;
      out.column = col;
      out.rowSpan = 1;
      out.colSpan = span;
      return out;
    }
    col += span;
    if (col >= spanCount) {
      col = 0;
      ++row;
    }
  }
  return out;  // Unreachable: index was found above.
}

}  // namespace lists
