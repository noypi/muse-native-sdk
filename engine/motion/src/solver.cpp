#include "motion/solver.h"

#include <cmath>

namespace motion {

SpringState advanceSpring(const SpringState& state, const SpringSpec& spec,
                          float target, float dtMs) noexcept {
  if (!(spec.stiffness > 0.0f)) return state;
  if (!(spec.dampingRatio > 0.0f)) return state;
  if (!(dtMs > 0.0f)) return state;
  if (!(target == target)) return state;

  const float dt = dtMs / 1000.0f;
  const float omega = std::sqrt(spec.stiffness);
  const float damping = 2.0f * spec.dampingRatio * omega;
  const float displacement = state.value - target;
  const float accel = -spec.stiffness * displacement - damping * state.velocity;

  SpringState next;
  next.velocity = state.velocity + accel * dt;
  next.value = state.value + next.velocity * dt;
  return next;
}

SpringState retargetSpring(const SpringState& state,
                           float /*newTarget*/) noexcept {
  // The target is caller-held (passed to advanceSpring); retargeting swaps
  // only the target, so value+velocity carry over unchanged.
  return state;
}

}  // namespace motion
