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
namespace {

// Sub-threshold deltas stay clean: the nonlinear curve is near-flat for
// small nudges, so resolved scales within this epsilon count as unchanged.
// Covers the 1.0 -> 1.01 nudge (delta ~0.01) while real steps like
// 1.0 -> 1.5 (delta ~0.28) still remeasure.
constexpr float kResolvedScaleEps = 0.02f;

}  // namespace

size_t RemeasureDirty(std::vector<TextNode>& nodes, float newFontScale,
                      float density) {
  (void)density;
  const float target = NonlinearScale(newFontScale);
  size_t remeasured = 0;
  for (auto& node : nodes) {
    if (node.dirty) {
      node.resolvedScale = target;
      node.dirty = false;
      ++remeasured;
      continue;
    }
    float delta = node.resolvedScale - target;
    if (delta < 0.0f) delta = -delta;
    if (delta <= kResolvedScaleEps) continue;
    node.resolvedScale = target;
    node.dirty = false;
    ++remeasured;
  }
  return remeasured;
}

}  // namespace muse::text
