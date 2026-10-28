#pragma once

#include <string>

namespace lists {

// States of the same collection node. `ContentWithFooter` encodes the plan's
// `Content+Footer` (content plus a trailing footer slot).
enum class CollectionState { Empty, Loading, Error, Content, ContentWithFooter };

// Slot node for a collection state. Every slot shares the same scrollable
// and the same semantics node as the collection, so `scrollable` is always
// true. `slot` is the node name only; the error slot never carries the
// underlying message, so no internals can leak through this surface.
struct SlotView {
  std::string slot;
  bool scrollable = true;
};

// Restored scroll position: viewport offset plus leading (first visible)
// item index in logical order.
struct ScrollPos {
  float offset = 0.0f;
  int leadingIndex = 0;
};

// Maps each collection state to its slot node. Pure function; never fails.
SlotView ResolveSlot(CollectionState st);

// Restores the scroll position across a state switch. Switches preserve the
// incoming offset/leading index, except switching into Empty, which resets
// to the top (offset 0, index 0) since there is no content position. Pure
// function of its inputs; `from` is accepted for symmetry with the call
// sites and does not change the outcome.
ScrollPos PreserveAcrossSwitch(ScrollPos before, CollectionState from,
                               CollectionState to);

}  // namespace lists
