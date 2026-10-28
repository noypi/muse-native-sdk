#pragma once

#include "layout/form_factor.h"

namespace layout {

// Tabletop (tent) posture layout: on HALF_OPENED splits `viewport` into
// media/content panes at the fold midpoint — media-above/content-below for a
// horizontal fold, media-left/content-right for a vertical fold. Any other
// state (Flat/Unknown) yields an inactive single pane: `content` fills the
// viewport and `media` is empty. Geometry only; the posture transition
// (`active` flip) is the hook the motion contract consumes. Never throws.
struct TabletopLayout {
  Rect media{};
  Rect content{};
  bool active = false;
};

TabletopLayout solveTabletop(const Rect& viewport, FoldState state,
                             FoldOrientation orientation) noexcept;

}  // namespace layout
