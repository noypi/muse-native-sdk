#pragma once

#include "layout/form_factor.h"

namespace layout {

// Splits `viewport` around a FULL-occlusion `hinge` into two panes abutting
// the hinge edges, leaving zero content inside the hinge rect. A hinge that
// spans the full viewport height (vertical band) yields left/right panes; a
// hinge spanning the full viewport width (horizontal band) yields top/bottom
// panes. Returns false (outputs untouched) when `occlusion` != Full, when
// the hinge rect is empty (non-positive w or h), or when either output is
// null. Never throws.
bool splitAroundHinge(const Rect& viewport, const Rect& hinge,
                      OcclusionType occlusion, Rect* outA,
                      Rect* outB) noexcept;

}  // namespace layout
