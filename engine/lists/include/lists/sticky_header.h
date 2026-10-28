#pragma once

#include <string>
#include <vector>

namespace lists {

// Header geometry consumed from the LAYOUT-M3X delegate shape (opaque here:
// this module never lays out, only reads `offset`/`height` per header).
struct HeaderGeo {
  std::string id;
  float offset = 0.0f;
  float height = 0.0f;
};

// Sticky-header input: headers sorted by offset in ascending order.
struct StickyState {
  std::vector<HeaderGeo> headers;
};

// One transition frame: `pinnedId` is the stuck header; `pushedById` names
// the incoming header while it overlaps the pinned header (empty otherwise);
// `yAdjust` is the pinned header's vertical shift (<= 0, 0 when not pushed).
struct StickyFrame {
  std::string pinnedId;
  std::string pushedById;
  float yAdjust = 0.0f;
};

// Pure function of `scrollOffset`: pins the last header at or above the
// offset and, when the next header overlaps the pinned header's bottom edge,
// reports the push-out transition. Examines at most the pinned header and
// its immediate successor, so an offset change never re-measures more than
// two headers. Empty state yields empty ids and zero adjust; never crashes.
StickyFrame UpdateSticky(const StickyState& s, float scrollOffset);

}  // namespace lists
