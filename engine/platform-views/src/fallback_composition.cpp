#include "pviews/fallback_composition.h"

// Fallback overlay composition with offscreen-copy degrade + touch mirror.
//
// Validation order mirrors src/z_order.cpp and T1's hybrid_embedding.cpp:
// id/geometry first, then the virtual-display branch. Every denial is
// fail-closed with the `{false, false, Overlay, InvalidGeometry}` shape and
// no throw. The refused-VD branch degrades to `{true, true, OffscreenCopy,
// None}`. Touch routing is a pure rect hit-test of the placed clip shifted
// by the stored transform translation.
FallbackResult placeFallback(FallbackSlot& slot, SdkViewId id, ClipRect clip,
                             Transform t,
                             bool virtualDisplayAvailable) noexcept {
  if (id.value == 0) {
    return FallbackResult{false, false, FallbackPath::Overlay,
                          FallbackError::InvalidGeometry};
  }
  if (clip.right <= clip.left || clip.bottom <= clip.top) {
    return FallbackResult{false, false, FallbackPath::Overlay,
                          FallbackError::InvalidGeometry};
  }
  if (clip.cornerRadius < 0) {
    return FallbackResult{false, false, FallbackPath::Overlay,
                          FallbackError::InvalidGeometry};
  }
  const FallbackPath path = virtualDisplayAvailable
                                ? FallbackPath::Overlay
                                : FallbackPath::OffscreenCopy;
  slot.occupied = true;
  slot.id = id;
  slot.clip = clip;
  slot.transform = t;
  slot.path = path;
  return FallbackResult{true, true, path, FallbackError::None};
}

TouchTarget routeFallbackTouch(const FallbackSlot& slot, int x,
                               int y) noexcept {
  if (!slot.occupied) {
    return TouchTarget::EngineHit;
  }
  const float left = static_cast<float>(slot.clip.left) + slot.transform.dx;
  const float top = static_cast<float>(slot.clip.top) + slot.transform.dy;
  const float right = static_cast<float>(slot.clip.right) + slot.transform.dx;
  const float bottom = static_cast<float>(slot.clip.bottom) + slot.transform.dy;
  if (static_cast<float>(x) >= left && static_cast<float>(x) < right &&
      static_cast<float>(y) >= top && static_cast<float>(y) < bottom) {
    return TouchTarget::SdkHit;
  }
  return TouchTarget::EngineHit;
}

void destroyFallback(FallbackSlot& slot) noexcept {
  slot.occupied = false;
  slot.id = SdkViewId{0};
  slot.clip = ClipRect{};
  slot.transform = Transform::identity();
  slot.path = FallbackPath::Overlay;
}
