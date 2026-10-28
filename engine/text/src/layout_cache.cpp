// FR-TEXT-007 / AC-TEXT-012: static layout cache implementation.
// Map keyed on the full LayoutKey; misses drive the committed T1 shaper
// for real layout content. invalidate(gen) drops only that generation.

#include "muse/text/layout_cache.h"

#include "muse/text/shaper.h"

namespace muse::text {

bool LayoutCache::KeyLess::operator()(const LayoutKey& a,
                                       const LayoutKey& b) const {
  if (a.textUtf8 != b.textUtf8) return a.textUtf8 < b.textUtf8;
  if (a.style.size_px != b.style.size_px)
    return a.style.size_px < b.style.size_px;
  if (a.style.letter_spacing_px != b.style.letter_spacing_px)
    return a.style.letter_spacing_px < b.style.letter_spacing_px;
  if (a.widthPx != b.widthPx) return a.widthPx < b.widthPx;
  if (a.scale != b.scale) return a.scale < b.scale;
  return a.fontSetGen < b.fontSetGen;
}

std::shared_ptr<const StaticLayout> LayoutCache::layoutStatic(
    const LayoutKey& key) {
  auto it = entries_.find(key);
  if (it != entries_.end()) return it->second;

  // Miss: shape once via the T1 pipeline. Scale folds into the effective
  // style so scaled layouts carry distinct real content.
  TextStyle effective = key.style;
  effective.size_px = key.style.size_px * key.scale;
  effective.letter_spacing_px = key.style.letter_spacing_px * key.scale;

  const auto* bytes =
      reinterpret_cast<const std::uint8_t*>(key.textUtf8.data());
  StatusOr<ShapedRun> shaped =
      shapeRun(bytes, key.textUtf8.size(), FontHandle{}, effective);

  auto layout = std::make_shared<StaticLayout>();
  if (shaped.ok()) {
    layout->shaped = *shaped;
    layout->measured = measureLine(ShapedRunView(layout->shaped), key.widthPx);
  } else {
    // Invalid input still yields a cacheable (empty) layout; never null,
    // never throws.
    layout->shaped = ShapedRun{};
    layout->measured = MeasuredLine{};
  }
  entries_.emplace(key, layout);
  return layout;
}

void LayoutCache::invalidate(std::uint64_t fontSetGen) {
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (it->first.fontSetGen == fontSetGen) {
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }
}

}  // namespace muse::text
