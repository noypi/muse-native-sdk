#ifndef MOTION_SOLVER_H_
#define MOTION_SOLVER_H_

namespace motion {

struct SpringState {
  float value = 0.0f;
  // Velocity in units per second.
  float velocity = 0.0f;
};

struct SpringSpec {
  float stiffness = 0.0f;
  float dampingRatio = 0.0f;
};

SpringState advanceSpring(const SpringState& state, const SpringSpec& spec,
                          float target, float dtMs) noexcept;
SpringState retargetSpring(const SpringState& state, float newTarget) noexcept;

}  // namespace motion

#endif  // MOTION_SOLVER_H_
