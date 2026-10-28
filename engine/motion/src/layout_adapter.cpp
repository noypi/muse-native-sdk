#include "motion/layout_adapter.h"

namespace motion {

Box interpolateBox(const Box& from, const Box& to, float t01) noexcept {
  float t = t01;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  Box out;
  out.x = from.x + (to.x - from.x) * t;
  out.y = from.y + (to.y - from.y) * t;
  out.w = from.w + (to.w - from.w) * t;
  out.h = from.h + (to.h - from.h) * t;
  return out;
}

}  // namespace motion
