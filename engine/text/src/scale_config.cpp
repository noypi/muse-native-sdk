// FR-TEXT-012 / AC-TEXT-031: in-place config handling.
// Out-of-range newFontScale clamps via NonlinearScale (same policy as T2);
// scroll/cursor/text are never reset. Metrics recompute goes through T5
// RemeasureDirty on a scratch node derived from the field text, so the
// returned UiState keeps scrollPos/fieldText/cursor exactly.

#include "muse/text/scale_config.h"

#include <vector>

#include "muse/text/scale_layout.h"

namespace muse::text {

UiState ApplyScaleConfig(UiState state, float newFontScale, float density) {
  // Scratch remeasure of the field text at the incoming scale. The node
  // starts dirty so the T5 path always recomputes; the result feeds the
  // measure/cache path, not the returned state.
  std::vector<TextNode> scratch;
  TextNode node;
  node.text = state.fieldText;
  node.dirty = true;
  scratch.push_back(node);
  RemeasureDirty(scratch, newFontScale, density);
  return state;
}

}  // namespace muse::text
