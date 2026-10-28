#include "motion/shared_element.h"

#include <cmath>

namespace motion {

namespace {

float lerpChannel(float from, float to, float t) {
  return from + (to - from) * t;
}

SharedFrame frameFromAnchor(const SharedAnchor& anchor, float t01) {
  SharedFrame frame;
  frame.box = anchor.box;
  frame.clipX = 0.0f;
  frame.clipY = 0.0f;
  frame.cornerRadius = anchor.cornerRadius;
  frame.t01 = t01;
  return frame;
}

}  // namespace

const SharedAnchor* motion_matchShared(const std::vector<SharedAnchor>& from,
                                       const std::vector<SharedAnchor>& to,
                                       const std::string& elementId) noexcept {
  (void)from;
  for (const SharedAnchor& anchor : to) {
    if (anchor.elementId == elementId) {
      return &anchor;
    }
  }
  return nullptr;
}

SharedFrame motion_interpolateShared(const SharedAnchor& from,
                                     const SharedAnchor& to,
                                     float t01) noexcept {
  if (!std::isfinite(t01)) {
    return frameFromAnchor(from, 0.0f);
  }
  float clamped = t01;
  if (clamped < 0.0f) clamped = 0.0f;
  if (clamped > 1.0f) clamped = 1.0f;
  if (clamped == 0.0f) {
    return frameFromAnchor(from, 0.0f);
  }
  if (clamped == 1.0f) {
    return frameFromAnchor(to, 1.0f);
  }
  SharedFrame frame;
  frame.box.x = lerpChannel(from.box.x, to.box.x, clamped);
  frame.box.y = lerpChannel(from.box.y, to.box.y, clamped);
  frame.box.w = lerpChannel(from.box.w, to.box.w, clamped);
  frame.box.h = lerpChannel(from.box.h, to.box.h, clamped);
  frame.clipX = 0.0f;
  frame.clipY = 0.0f;
  frame.cornerRadius = lerpChannel(from.cornerRadius, to.cornerRadius, clamped);
  frame.t01 = clamped;
  return frame;
}

}  // namespace motion
