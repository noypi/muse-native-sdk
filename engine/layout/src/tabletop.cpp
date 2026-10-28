#include "layout/tabletop.h"

namespace layout {

TabletopLayout solveTabletop(const Rect& viewport, FoldState state,
                             FoldOrientation orientation) noexcept {
  TabletopLayout out;
  if (state != FoldState::HalfOpened) {
    out.media = Rect{0.0f, 0.0f, 0.0f, 0.0f};
    out.content = viewport;
    out.active = false;
    return out;
  }
  out.active = true;
  if (orientation == FoldOrientation::Horizontal) {
    const float half = viewport.h * 0.5f;
    out.media = Rect{viewport.x, viewport.y, viewport.w, half};
    out.content =
        Rect{viewport.x, viewport.y + half, viewport.w, viewport.h - half};
  } else {
    const float half = viewport.w * 0.5f;
    out.media = Rect{viewport.x, viewport.y, half, viewport.h};
    out.content =
        Rect{viewport.x + half, viewport.y, viewport.w - half, viewport.h};
  }
  return out;
}

}  // namespace layout
