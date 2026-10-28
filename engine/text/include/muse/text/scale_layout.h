#pragma once

// FR-TEXT-012 / AC-TEXT-030: scale-aware incremental remeasure.
// Per-node resolved scale via the T2 NonlinearScale curve; only dirty or
// stale nodes reshape. Clean nodes already at the incoming scale are skipped.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace muse::text {

// One measurable paragraph/run. resolvedScale carries the NonlinearScale
// output the node was last measured at; styleGen rides the LayoutCache key;
// dirty forces remeasure on the next pass.
struct TextNode {
  std::string text;
  float sizePx = 0.0f;
  float resolvedScale = 1.0f;
  uint64_t styleGen = 0u;
  bool dirty = false;
};

// Remeasures every node whose stored resolvedScale differs from
// NonlinearScale(newFontScale) by more than the near-flat epsilon,
// plus every node flagged dirty. Each
// remeasured node gets resolvedScale updated to the new scale and dirty
// cleared; text/sizePx/styleGen are preserved. Returns the remeasure count.
// density is accepted for the cache-key width derivation and reserved for
// the measure path; it does not change which nodes are stale.
size_t RemeasureDirty(std::vector<TextNode>& nodes, float newFontScale,
                      float density);

}  // namespace muse::text
