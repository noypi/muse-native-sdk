#include "engine/graphics/foldable/foldable.h"

namespace foldable {

FoldPosture postureFrom(const WindowMetrics& m, const LayoutInfo& i,
                        float hingeAngleDeg) noexcept {
  // No folding feature -> no posture signal (never guess FLAT).
  if (!i.hasFoldingFeature) {
    return FoldPosture::kUnknown;
  }
  // Degenerate / empty metrics -> UNKNOWN. Using !(v > 0) so NaN also fails.
  if (!(m.bounds.width > 0.0f) || !(m.bounds.height > 0.0f)) {
    return FoldPosture::kUnknown;
  }
  // Hinge angle must be a finite value strictly inside (0, 360).
  // Covers 0, 360, negative, NaN, inf, and out-of-range values.
  if (!(hingeAngleDeg > 0.0f) || !(hingeAngleDeg < 360.0f)) {
    return FoldPosture::kUnknown;
  }
  // Fully open band -> FLAT.
  if (hingeAngleDeg >= 150.0f) {
    return FoldPosture::kFlat;
  }
  // Mid-range angles -> half-opened; horizontal hinge reads as tabletop.
  if (i.isTabletopOrientation) {
    return FoldPosture::kTabletop;
  }
  return FoldPosture::kHalfOpened;
}

}  // namespace foldable
