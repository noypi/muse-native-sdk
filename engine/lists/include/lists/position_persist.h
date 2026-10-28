#pragma once

#include <string>

#include "lists/myers_diff.h"
#include "lists/stable_key.h"

namespace lists {

// Destination identity for per-destination position persistence
// (navigation owns destination identity; this is the consume-only handle).
struct DestId {
  std::string id;
};

// Persisted scroll position written through the STATE-PERSIST snapshot:
// exact offset + logical index + anchor key/fraction, tagged per
// destination so one destination's snapshot never leaks into another.
struct Snapshot {
  DestId dest;
  float offsetPx = 0.0f;
  int index = 0;
  ItemKey anchor;
  float anchorFrac = 0.0f;
};

// Restored scroll position per the NAV-RESTORE contract. `velocityPxS` is
// always 0.0f (rest-at-rest: in-flight velocity is never restored).
struct Restored {
  float offsetPx = 0.0f;
  int index = 0;
  float velocityPxS = 0.0f;
};

// Records the current position for destination `d`.
Snapshot savePos(DestId d, float offsetPx, int index, ItemKey anchor,
                 float anchorFrac);

// Restores snapshot `s` against the current `data`. When the anchor key
// still survives, its current position wins; otherwise falls back to the
// nearest surviving index (saved index clamped to [0, size-1], empty data
// restores index 0 / offset 0). Never returns an out-of-range index, and
// never restores velocity.
Restored restorePos(DestId d, const Snapshot& s, const KeyedList& data);

}  // namespace lists
