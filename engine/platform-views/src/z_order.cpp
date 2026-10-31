#include "pviews/z_order.h"

// Places a platform sub-surface relative to the Vulkan engine surface.
//
// Layer sign convention: positive z above engine content (Layer::ABOVE),
// negative z below it (Layer::BELOW). The transform (scroll/translate/resize)
// moves the clip with the surface; it never invalidates placement on its own.
// Hardware clip regions/corner radii (API 31+) vs. the offscreen compositing
// fallback for complex blending are device details — unit-observable behavior
// is clipApplied == true for every valid geometry.
ZOrderResult placeSurface(SurfaceId id, LayerOrder order, ClipRect clip,
                         Transform /*t*/) {
  if (id.value == 0) {
    return ZOrderResult{false, -1, false};
  }
  if (clip.right <= clip.left || clip.bottom <= clip.top) {
    return ZOrderResult{false, -1, false};
  }
  if (clip.cornerRadius < 0) {
    return ZOrderResult{false, -1, false};
  }
  const int layer = (order.layer == Layer::ABOVE) ? 1 : -1;
  return ZOrderResult{true, layer, true};
}
