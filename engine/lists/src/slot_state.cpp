#include "lists/slot_state.h"

namespace lists {

SlotView ResolveSlot(CollectionState st) {
  SlotView v;
  // All slots live on the same scrollable/semantics node as the collection.
  v.scrollable = true;
  switch (st) {
    case CollectionState::Empty:
      v.slot = "empty";
      break;
    case CollectionState::Loading:
      v.slot = "loading";
      break;
    case CollectionState::Error:
      // Name only: error details stay out of the slot view.
      v.slot = "error";
      break;
    case CollectionState::Content:
      v.slot = "content";
      break;
    case CollectionState::ContentWithFooter:
      v.slot = "content+footer";
      break;
  }
  return v;
}

ScrollPos PreserveAcrossSwitch(ScrollPos before, CollectionState /*from*/,
                               CollectionState to) {
  // Empty holds no content position, so entering it resets to the top.
  // Every other switch keeps the same scrollable, hence the same position.
  if (to == CollectionState::Empty) {
    return ScrollPos{0.0f, 0};
  }
  return before;
}

}  // namespace lists
