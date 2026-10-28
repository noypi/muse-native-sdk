#ifndef MOTION_LAYOUT_ADAPTER_H_
#define MOTION_LAYOUT_ADAPTER_H_

namespace motion {

struct Box {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

Box interpolateBox(const Box& from, const Box& to, float t01) noexcept;

}  // namespace motion

#endif  // MOTION_LAYOUT_ADAPTER_H_
