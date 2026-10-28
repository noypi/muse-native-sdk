#pragma once

#include <string>
#include <vector>

#include "lists/stable_key.h"

namespace lists {

// Layout mode parameter over the single shared recycler/diff path.
// Modes never fork recycling or diffing; they only change frame geometry.
enum class CollectionMode { List, GridSpans, Staggered };

// Layout request: `mode` selects geometry; `spanCount` bounds grid spans
// (values < 1 are clamped to 1 so a zero spanCount still lays out).
struct CollectionSpec {
  CollectionMode mode = CollectionMode::List;
  int spanCount = 1;
};

// Item set in logical order; stable-key identity, never positional.
using ItemList = std::vector<ItemKey>;

// Realized-window frame for one item. `span` is the grid cell span
// (always 1 in List mode, within [1, spanCount] in grid modes).
struct ItemFrame {
  ItemKey key;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  int span = 1;
};

// Realized-window layout: one frame per item in logical order, plus the
// shared recycler pool keys. `poolKeys` is a pure function of the item
// set (heterogeneous holder types pooled per shape key) and is identical
// across modes for the same items — one recycler/diff path.
struct CollectionLayout {
  std::vector<ItemFrame> frames;
  std::vector<std::string> poolKeys;
};

// Builds frames for every item in logical order under `spec`, with pool
// keys shared across modes. Pure function of its inputs; never crashes
// on empty items or a zero spanCount.
CollectionLayout BuildLayout(CollectionSpec spec, const ItemList& items);

}  // namespace lists
