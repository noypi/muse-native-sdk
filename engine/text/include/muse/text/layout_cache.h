#pragma once

// FR-TEXT-007 / AC-TEXT-012: static layout cache keyed on
// text/style/width/scale/font-set generation. Unchanged inputs reuse the
// same layout across frames without reshape; any key change is a miss.
// Pure C++: no JNI types, no network, no downloads.

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "muse/text/text_types.h"

namespace muse::text {

// Full cache key. Every field participates in equality: text, style,
// width budget, scale, and font-set generation.
struct LayoutKey {
  std::string textUtf8;
  TextStyle style;
  float widthPx = 0.0f;
  float scale = 1.0f;
  std::uint64_t fontSetGen = 0;
};

// Cached static layout content. Built once per key via the T1 shaper
// (shapeRun + measureLine); callers hold shared ownership so a hit can
// return the pointer-identical instance.
struct StaticLayout {
  ShapedRun shaped;
  MeasuredLine measured;
};

// Default-constructible owner class. layoutStatic/invalidate are member
// functions with the verbatim signatures from the plan.
class LayoutCache {
 public:
  LayoutCache() = default;

  std::shared_ptr<const StaticLayout> layoutStatic(const LayoutKey& key);
  void invalidate(std::uint64_t fontSetGen);

 private:
  struct KeyLess {
    bool operator()(const LayoutKey& a, const LayoutKey& b) const;
  };
  std::map<LayoutKey, std::shared_ptr<const StaticLayout>, KeyLess> entries_;
};

}  // namespace muse::text
