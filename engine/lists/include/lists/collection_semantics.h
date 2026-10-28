#pragma once

#include <vector>

#include "lists/collection_contract.h"
#include "lists/stable_key.h"

namespace lists {

// Windowed collection node feeding the A11Y-SEMANTICS mirror-thread shape.
// `totalCount` is the logical item count; `realizedCount` is the live window
// size (small for a 10k-item collection); `leadingIndex` is the first visible
// logical index. Consumed as an opaque sink: no mirror-thread code lives here.
struct CollectionNode {
  // Keeps the T11 braced form `CollectionNode{t, r, l}` warning-clean under
  // -Werror=missing-field-initializers now that T12 appends members below:
  // an aggregate init list shorter than the member count would error, while
  // this constructor initializes only the window fields (new members keep
  // their defaults). Existing field order, names, and defaults unchanged.
  CollectionNode() = default;
  CollectionNode(int total, int realized, int leading)
      : totalCount(total), realizedCount(realized), leadingIndex(leading) {}
  int totalCount = 0;
  int realizedCount = 0;
  int leadingIndex = 0;
  // Additive T12 fields: ordered logical items plus grid geometry. Existing
  // semantics functions touch only the window fields above.
  std::vector<ItemKey> items;
  CollectionMode mode = CollectionMode::List;
  int spanCount = 1;
  std::vector<int> spans;
};

// Scroll direction for an accessibility scroll action.
enum class ScrollDir { Forward, Backward };

// Accessibility scroll actions for a collection node. `scrollable` is always
// true: windowed realization never clears the scrollable flag, even when
// realizedCount < totalCount, and empty/fully-realized nodes stay scrollable.
struct A11yActions {
  bool canScrollForward = false;
  bool canScrollBackward = false;
  bool scrollable = true;
};

// Outcome of performing a scroll action: the new leading index plus whether
// the window actually moved (false when already clamped at the edge).
struct ScrollResult {
  int leadingIndex = 0;
  bool moved = false;
};

// Exposes the scroll actions for a collection node. Pure function of the
// window position; never fails.
A11yActions GetScrollActions(const CollectionNode& n);

// Steps the realized window by one realized window (realizedCount) in `d`,
// clamped to [0, totalCount - realizedCount]; updates `n.leadingIndex` and
// returns the new leading index.
ScrollResult PerformScrollAction(CollectionNode& n, ScrollDir d);

}  // namespace lists
