#include "lists/collection_contract.h"

namespace lists {
namespace {

// Single shared recycler/diff accounting: pool keys derive only from the
// item set shape, never from the layout mode. Heterogeneous holder types
// are pooled per shape key (text vs media here, split by key hash), so all
// modes share one recycler path with no per-mode forks.
std::vector<std::string> SharedPoolKeys(const ItemList& items) {
  if (items.empty()) return {};
  bool hasMedia = false;
  for (const auto& it : items) {
    if (!it.key.empty() && (it.key.back() % 2 == 0)) {
      hasMedia = true;
      break;
    }
  }
  std::vector<std::string> keys{"holder:text"};
  if (hasMedia) keys.push_back("holder:media");
  return keys;
}

constexpr float kRowHeight = 1.0f;

}  // namespace

CollectionLayout BuildLayout(CollectionSpec spec, const ItemList& items) {
  CollectionLayout out;
  out.poolKeys = SharedPoolKeys(items);
  out.frames.reserve(items.size());
  if (items.empty()) return out;

  const int spans = spec.spanCount < 1 ? 1 : spec.spanCount;

  switch (spec.mode) {
    case CollectionMode::List: {
      // Full-width stack in logical order.
      for (size_t i = 0; i < items.size(); ++i) {
        ItemFrame f;
        f.key = items[i];
        f.x = 0.0f;
        f.y = static_cast<float>(i) * kRowHeight;
        f.w = 1.0f;
        f.h = kRowHeight;
        f.span = 1;
        out.frames.push_back(f);
      }
      break;
    }
    case CollectionMode::GridSpans: {
      // Pack span-1 cells left to right across `spans` columns.
      // Span varies deterministically (full-width every Nth item) but is
      // always bounded by [1, spans]; frame order stays logical.
      const float colW = 1.0f / static_cast<float>(spans);
      int col = 0;
      int row = 0;
      for (size_t i = 0; i < items.size(); ++i) {
        int span = (i % 4 == 3) ? spans : 1;
        if (span < 1) span = 1;
        if (span > spans) span = spans;
        if (col + span > spans) {
          col = 0;
          ++row;
        }
        ItemFrame f;
        f.key = items[i];
        f.x = static_cast<float>(col) * colW;
        f.y = static_cast<float>(row) * kRowHeight;
        f.w = static_cast<float>(span) * colW;
        f.h = kRowHeight;
        f.span = span;
        out.frames.push_back(f);
        col += span;
        if (col >= spans) {
          col = 0;
          ++row;
        }
      }
      break;
    }
    case CollectionMode::Staggered: {
      // Round-robin columns with varying heights; logical order kept.
      const float colW = 1.0f / static_cast<float>(spans);
      std::vector<float> colY(static_cast<size_t>(spans), 0.0f);
      for (size_t i = 0; i < items.size(); ++i) {
        const size_t col = i % static_cast<size_t>(spans);
        const float h = kRowHeight * (1.0f + 0.5f * static_cast<float>(i % 2));
        ItemFrame f;
        f.key = items[i];
        f.x = static_cast<float>(col) * colW;
        f.y = colY[col];
        f.w = colW;
        f.h = h;
        f.span = 1;
        out.frames.push_back(f);
        colY[col] += h;
      }
      break;
    }
  }
  return out;
}

}  // namespace lists
