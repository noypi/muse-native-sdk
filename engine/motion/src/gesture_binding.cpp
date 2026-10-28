#include "motion/gesture_binding.h"

namespace motion {

GestureBinding::GestureBinding(ITransformWriter* writer) : writer_(writer) {}

void GestureBinding::onSample(const InputSample& s) {
  // Strictly-increasing tMs required; stale or out-of-order samples are
  // ignored and the writer keeps holding the last value.
  if (hasSample_ && !(s.tMs > lastTMs_)) return;
  hasSample_ = true;
  lastTMs_ = s.tMs;
  spring_.value = s.position;
  spring_.velocity = s.velocity;
  if (writer_ != nullptr) writer_->writeTransform(s.position, s.tMs);
}

void GestureBinding::onRelease(const InputSample& s) {
  // Release hands the measured velocity to the T1 spring: the spring starts
  // at the release position with initial velocity == release velocity.
  SpringState seeded;
  seeded.value = s.position;
  seeded.velocity = s.velocity;
  // Reuse the T1 retarget helper so the handoff keeps value+velocity
  // continuity by construction (target is caller-held).
  spring_ = retargetSpring(seeded, s.position);
  hasSample_ = true;
  lastTMs_ = s.tMs;
}

}  // namespace motion
