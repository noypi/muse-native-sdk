#ifndef MOTION_SPRING_H_
#define MOTION_SPRING_H_

// Interruptible spring solver with velocity-preserving retarget
// (FR-MOTION-001 / AC-MOTION-002; consumes the FR-MOTION-008 velocity-handoff
// shape: the caller hands the current velocity in via `initialVelocity`).
//
// Retarget keeps the current (x, v) and only swaps the target: each call
// advances the spring one semi-implicit Euler step from (s.x,
// initialVelocity) toward `target`. The caller drives the animation by
// calling motion_retarget every frame with the latest target.
//
// Error channel: the declared return type RetargetResult carries no
// MotionError field, so invalid inputs are reported observably — the input
// state is returned unchanged with settled == false, and nothing throws.
// See unit-tests/T1.md ASSUMPTION (error channel).

struct SpringState {
  double x = 0.0;
  // Velocity in units per second.
  double v = 0.0;
  double stiffness = 0.0;
  double damping = 0.0;
};

struct RetargetResult {
  SpringState state;
  bool settled = false;
};

enum class MotionError {
  kOk,
  kBadStiffness,
  kBadTimestep,
};

// One semi-implicit Euler step toward `target`, adopting `initialVelocity`
// as the current velocity (velocity-preserving handoff). Never throws:
// non-positive or non-finite stiffness/timestep yields the input state
// unchanged with settled == false.
RetargetResult motion_retarget(const SpringState& s, double target,
                              double initialVelocity, double dt) noexcept;

#endif  // MOTION_SPRING_H_
