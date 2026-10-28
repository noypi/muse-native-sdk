// FR-TEXT-012 / AC-TEXT-030: scale-aware incremental remeasure.
// A node is stale when its stored resolvedScale differs from the freshly
// resolved NonlinearScale(newFontScale), or when explicitly flagged dirty.
// Remeasure here updates the resolved scale and clears the flag; the actual
// reshape goes through the shaper/LayoutCache keyed on
// text/style/width/scale/fontSetGen, so skipped nodes keep their cached
// layouts untouched.

#include "muse/text/scale_layout.h"

#include "muse/text/font_scale.h"

namespace muse::text {

size_t RemeasureDirty(std::vector<TextNode>& nodes, float newFontScale,
                      float density) {
  (void)density;
  const float target = NonlinearScale(newFontScale);
  size_t remeasured = 0;
  for (auto& node : nodes) {
    if (!node.dirty && node.resolvedScale == target) continue;
    node.resolvedScale = target;
    node.dirty = false;
    ++remeasured;
  }
  return remeasured;
}

}  // namespace muse::text
