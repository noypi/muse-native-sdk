#include "layout/hinge_split.h"

namespace layout {

bool splitAroundHinge(const Rect& viewport, const Rect& hinge,
                      OcclusionType occlusion, Rect* outA,
                      Rect* outB) noexcept {
  if (outA == nullptr || outB == nullptr) {
    return false;
  }
  if (occlusion != OcclusionType::Full) {
    return false;
  }
  if (hinge.w <= 0.0f || hinge.h <= 0.0f) {
    return false;
  }
  // Axis rule: a hinge spanning the full viewport height is a vertical band
  // (left/right panes); spanning the full width is a horizontal band
  // (top/bottom panes). Otherwise fall back to the longer axis.
  const bool spansHeight =
      hinge.y <= viewport.y && hinge.y + hinge.h >= viewport.y + viewport.h;
  const bool spansWidth =
      hinge.x <= viewport.x && hinge.x + hinge.w >= viewport.x + viewport.w;
  bool vertical;
  if (spansHeight && !spansWidth) {
    vertical = true;
  } else if (spansWidth && !spansHeight) {
    vertical = false;
  } else {
    vertical = hinge.h >= hinge.w;
  }
  if (vertical) {
    outA->x = viewport.x;
    outA->y = viewport.y;
    outA->w = hinge.x - viewport.x;
    outA->h = viewport.h;
    outB->x = hinge.x + hinge.w;
    outB->y = viewport.y;
    outB->w = (viewport.x + viewport.w) - (hinge.x + hinge.w);
    outB->h = viewport.h;
  } else {
    outA->x = viewport.x;
    outA->y = viewport.y;
    outA->w = viewport.w;
    outA->h = hinge.y - viewport.y;
    outB->x = viewport.x;
    outB->y = hinge.y + hinge.h;
    outB->w = viewport.w;
    outB->h = (viewport.y + viewport.h) - (hinge.y + hinge.h);
  }
  return true;
}

}  // namespace layout
