#pragma once

namespace lists {

// Scroll-physics curve shaping per the SPEC-LISTS-SCROLLING Scroll Physics
// Curves table. Consumes the FR-MOTION-008 curve/interruption contract:
// the caller selects the shaping; integrate stays a pure per-frame step.
enum class CurveKind {
  AndroidOverscroll,
  SpringBounce,
  Custom,
};

// Tuning for integrate. `kind` selects the curve; the remaining fields are
// the custom(stiffness, damping, friction, flingMultiplier, rubberBand)
// shaping consumed under the same call shape for every kind.
struct CurveParams {
  CurveKind kind = CurveKind::AndroidOverscroll;
  float stiffness = 180.0f;
  float damping = 18.0f;
  float friction = 0.35f;
  float flingMultiplier = 1.0f;
  float rubberBand = 0.45f;
};

// Render-thread scroll state: content offset and fling velocity.
struct PhysicsState {
  float offsetPx = 0.0f;
  float velocityPxS = 0.0f;
};

// Predicted touch sample from the SPEC-INPUT-GESTURES stream. A lifted
// finger carries zero driving velocity; a down finger drives toward
// `velocityPxS`.
struct PredictedSample {
  float offsetPx = 0.0f;
  float velocityPxS = 0.0f;
};

// Injected VSYNC deadline: the frame step in seconds (never wall-clock).
struct VsyncDeadline {
  float dtS = 0.0f;
};

// Frame-rate-independent semi-implicit integrate on the render thread.
// Pure C++ with no JS/JNI. A non-positive or non-finite dt is a no-op that
// returns `s` unchanged; a state at rest with no driving touch input stays
// at rest.
PhysicsState integrate(PhysicsState s, PredictedSample sample,
                       VsyncDeadline vsync, CurveParams curve);

}  // namespace lists
