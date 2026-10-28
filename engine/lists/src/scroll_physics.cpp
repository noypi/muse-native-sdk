#include "lists/scroll_physics.h"

#include <cmath>

namespace lists {
namespace {

// Effective viscous decay rate (1/s) per curve kind. All three curves share
// the same exactly-integrated decay shape so an identical fling converges
// to the same distance at any frame rate; the per-kind mapping only scales
// how fast the fling settles.
float effectiveFriction(const CurveParams& curve) {
  switch (curve.kind) {
    case CurveKind::AndroidOverscroll:
      // Platform overscroll glide: light friction on top of the base.
      return curve.friction > 0.0f ? curve.friction : 0.35f;
    case CurveKind::SpringBounce:
      // Spring return adds damping-derived settling without an anchor in
      // free fling, so it still converges to a finite distance.
      return (curve.friction > 0.0f ? curve.friction : 0.35f) +
             (curve.damping > 0.0f ? curve.damping / 50.0f : 0.0f);
    case CurveKind::Custom:
      // Custom stiffness/damping/friction shaping under the same call.
      return (curve.friction > 0.0f ? curve.friction : 0.35f) +
             (curve.damping > 0.0f ? curve.damping / 50.0f : 0.0f) +
             (curve.stiffness > 0.0f ? curve.stiffness / 4000.0f : 0.0f);
  }
  return 0.35f;
}

}  // namespace

PhysicsState integrate(PhysicsState s, PredictedSample sample,
                       VsyncDeadline vsync, CurveParams curve) {
  const float dt = vsync.dtS;
  // Zero-dt (or negative/NaN) frame: strict no-op, never NaN/divergent.
  if (!(dt > 0.0f)) return s;
  // Non-finite state recovers to rest instead of sticking: keep a finite
  // offset (fall back to 0 when the offset itself is non-finite) and zero
  // the velocity, so a single NaN from upstream touch prediction cannot
  // poison the fling forever. Choice documented: {offset, 0}, not {0, 0},
  // so a NaN velocity alone does not jump the scroll position.
  if (!(s.offsetPx == s.offsetPx && s.velocityPxS == s.velocityPxS)) {
    PhysicsState rest;
    rest.offsetPx = (s.offsetPx == s.offsetPx) ? s.offsetPx : 0.0f;
    rest.velocityPxS = 0.0f;
    return rest;
  }

  const float touchV =
      (sample.velocityPxS == sample.velocityPxS) ? sample.velocityPxS : 0.0f;

  // Rest stays at rest: no driving touch input and no fling velocity means
  // no spontaneous motion and no phantom velocity.
  if (s.velocityPxS == 0.0f && touchV == 0.0f) return s;

  float friction = effectiveFriction(curve);
  if (!(friction > 1e-6f)) friction = 1e-6f;
  const float multiplier =
      (curve.flingMultiplier == curve.flingMultiplier &&
       curve.flingMultiplier > 0.0f)
          ? curve.flingMultiplier
          : 1.0f;

  // Exact integral of v(t) = v0 * exp(-f * t) over this frame. Summed over
  // frames the travelled distance is v0/f regardless of dt, which is what
  // makes 60/90/120/144Hz flings converge to the same distance.
  const float decay = std::exp(-friction * dt);
  const float travelled =
      s.velocityPxS * (1.0f - decay) / friction * multiplier;

  PhysicsState out;
  out.offsetPx = s.offsetPx + travelled;
  out.velocityPxS = s.velocityPxS * decay;

  // Consume the predicted touch stream: a live finger (non-zero driving
  // velocity) pulls the fling velocity toward the touch velocity. A lifted
  // finger (zero) leaves the exact decay above untouched so flings stay
  // rate-independent.
  if (touchV != 0.0f) {
    const float grip = dt < 0.1f ? dt * 10.0f : 1.0f;
    out.velocityPxS += (touchV - out.velocityPxS) * grip;
  }

  // Snap denormal drift to rest so long flings terminate cleanly.
  if (std::fabs(out.velocityPxS) < 1e-4f && touchV == 0.0f) {
    out.velocityPxS = 0.0f;
  }
  return out;
}

}  // namespace lists
