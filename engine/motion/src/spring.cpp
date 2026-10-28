#include "motion/spring.h"

#include <cmath>

namespace {

// Settle band: position within 1e-3 of target with near-zero velocity
// counts as settled. The plan pins no threshold; this matches the
// contract's directional settle rule (far-from-target in flight => false,
// converged near target => true).
constexpr double kSettleEps = 1e-3;

bool invalidStiffness(double stiffness) noexcept {
  return !(stiffness > 0.0) || !std::isfinite(stiffness);
}

bool invalidTimestep(double dt) noexcept {
  return !(dt > 0.0) || !std::isfinite(dt);
}

}  // namespace

RetargetResult motion_retarget(const SpringState& s, double target,
                              double initialVelocity, double dt) noexcept {
  // Observable error contract: bad stiffness/timestep leaves the state
  // unchanged and reports not-settled; no exception crosses the API.
  // The !(x > 0.0) form also rejects NaN.
  if (invalidStiffness(s.stiffness) || invalidTimestep(dt)) {
    RetargetResult r;
    r.state = s;
    r.settled = false;
    return r;
  }

  // Velocity-preserving handoff: the step starts from the caller-provided
  // velocity, so an interrupt carries in-flight momentum into the new
  // target instead of snapping or kicking.
  const double v0 = initialVelocity;
  const double displacement = s.x - target;
  const double accel =
      -s.stiffness * displacement - s.damping * v0;

  SpringState next;
  next.v = v0 + accel * dt;
  next.x = s.x + next.v * dt;
  next.stiffness = s.stiffness;
  next.damping = s.damping;

  RetargetResult r;
  r.state = next;
  r.settled = std::fabs(next.x - target) <= kSettleEps &&
              std::fabs(next.v) <= kSettleEps;
  return r;
}
