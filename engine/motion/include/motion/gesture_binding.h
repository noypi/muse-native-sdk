#ifndef MOTION_GESTURE_BINDING_H_
#define MOTION_GESTURE_BINDING_H_

#include "motion/solver.h"

namespace motion {

// Render transform sink. Implemented by the rendering layer; the binding
// calls it synchronously in the same onSample call (same frame, same tMs).
class ITransformWriter {
 public:
  virtual ~ITransformWriter() = default;
  virtual void writeTransform(float value, float tMs) = 0;
};

// Minimal gesture sample (input-owned shape). Velocity is in units/s,
// matching the T1 solver state velocity (SpringState::velocity, integrated
// with dt in seconds) and the Android VelocityTracker convention.
struct InputSample {
  float position = 0.0f;
  float velocity = 0.0f;
  float tMs = 0.0f;
};

// Direct C++ call path sample -> transform with no JS/JNI boundary.
// Drag samples write through to the injected writer in the same call;
// release seeds the T1 spring state carrying position + velocity.
class GestureBinding {
 public:
  explicit GestureBinding(ITransformWriter* writer);

  void onSample(const InputSample& s);
  void onRelease(const InputSample& s);

  const SpringState& springState() const noexcept { return spring_; }

 private:
  ITransformWriter* writer_;
  SpringState spring_;
  float lastTMs_ = 0.0f;
  bool hasSample_ = false;
};

}  // namespace motion

#endif  // MOTION_GESTURE_BINDING_H_
