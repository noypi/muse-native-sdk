#include "layout/sliver.h"

namespace layout {

SliverDelegate::SliverDelegate(SliverConfig c)
    : config_(c), scrollOffset_(0.0f) {}

void SliverDelegate::setScrollOffset(float px) noexcept {
  // !(px >= 0) maps both negatives and NaN to 0; over-range values are kept
  // and saturate the fraction at 1.0 in collapseFraction().
  scrollOffset_ = (px >= 0.0f) ? px : 0.0f;
}

void SliverDelegate::measure(NodeId id, float availW, float availH) {
  (void)id;
  (void)availW;
  (void)availH;
  // No-op: frames are installed via frameFor() before dispatch.
}

void SliverDelegate::arrange(NodeId id, Box bounds) {
  (void)id;
  (void)bounds;
  // No-op: the forwarded bounds already equal the installed frame.
}

bool SliverDelegate::handles(NodeId id) const {
  return id != kInvalidNodeId;
}

float SliverDelegate::collapseFraction() const noexcept {
  const float range = config_.expandedH - config_.collapsedH;
  if (!(range > 0.0f)) {
    // Degenerate config (no collapse travel): any positive offset counts as
    // fully collapsed. Avoids a divide-by-zero producing NaN.
    return scrollOffset_ > 0.0f ? 1.0f : 0.0f;
  }
  const float f = scrollOffset_ / range;
  if (!(f >= 0.0f)) {
    return 0.0f;
  }
  if (f > 1.0f) {
    return 1.0f;
  }
  return f;
}

bool SliverDelegate::frameFor(NodeId id, const Box& rootBox, int childIndex,
                              float naturalW, float naturalH,
                              Box* out) const noexcept {
  (void)id;
  if (out == nullptr) {
    return false;
  }
  if (childIndex < 0) {
    // The delegate root keeps its solved viewport frame.
    *out = rootBox;
    return true;
  }
  const float fraction = collapseFraction();
  const float toolbarH =
      config_.expandedH + fraction * (config_.collapsedH - config_.expandedH);
  const float contentY = rootBox.y + toolbarH;
  switch (childIndex) {
    case 0: {
      // Toolbar: full viewport width, interpolated height.
      *out = Box{rootBox.x, rootBox.y, rootBox.w, toolbarH};
      return true;
    }
    case 1: {
      // Content: natural flex size, parked directly under the toolbar.
      *out = Box{rootBox.x, contentY, naturalW, naturalH};
      return true;
    }
    case 2: {
      // Pinned header: sticks at the viewport top once scrolled past.
      const float pinnedY = contentY > rootBox.y + scrollOffset_
                                ? contentY
                                : rootBox.y + scrollOffset_;
      *out = Box{rootBox.x, pinnedY, rootBox.w, config_.pinnedH};
      return true;
    }
    default:
      break;
  }
  return false;
}

}  // namespace layout
