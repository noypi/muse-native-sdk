#pragma once

#include "lists/collection_contract.h"
#include "lists/collection_semantics.h"
#include "lists/stable_key.h"

namespace lists {

// Stable logical position of one item within a collection node, including
// span cells. `index` is the logical position in `CollectionNode::items`
// (-1 when `key` is absent); `count` is the node size; `row`/`column` are
// the T6 layout cell of the item's leading corner; `rowSpan`/`colSpan` are
// its cell extents (always 1 in List mode).
struct CollectionItemInfo {
  int index = -1;
  int count = 0;
  int row = -1;
  int column = 0;
  int rowSpan = 1;
  int colSpan = 1;
};

// Reports the stable logical position of `key` in `n`: index/count from
// logical order, row/column/span from the T6 layout geometry. List-mode
// items report row == index, column == 0, spans 1. Grid-mode cells pack
// left to right honouring each item's span (from `CollectionNode::spans`,
// default 1, clamped to [1, spanCount]); a span-N item reports colSpan N.
// An absent key reports index == -1 with count intact and spans 1.
CollectionItemInfo GetItemInfo(const CollectionNode& n, ItemKey k);

}  // namespace lists
