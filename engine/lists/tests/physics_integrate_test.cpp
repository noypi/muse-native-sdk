// T2 contract tests — FR-LISTS-003 / AC-LISTS-008 (TC-LISTS-008).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services, no wall-clock sleeps. VSYNC timing is
// injected via VsyncDeadline values constructed in-test.
// Binds to the T2 `Interface:` line:
//   `PhysicsState integrate(PhysicsState s, PredictedSample sample,
//    VsyncDeadline vsync, CurveParams curve);`
//   — pure C++, no JS/JNI; curve selects android-overscroll |
//   spring-bounce | custom.
// Include path follows the engine convention:
//   #include "lists/scroll_physics.h" with engine/lists/include on the path.
//
// ASSUMPTION (type member spellings): the plan/Interface line names the
// four types but does not settle their members. This test proposes:
//   lists::PhysicsState{.offsetPx, .velocityPxS}
//   lists::PredictedSample{.offsetPx, .velocityPxS}
//   lists::VsyncDeadline{.dtS}          (injected frame step, seconds)
//   lists::CurveKind::{AndroidOverscroll, SpringBounce, Custom}
//   lists::CurveParams{.kind, .stiffness, .damping, .friction,
//                      .flingMultiplier, .rubberBand}
// A helper `mkCurve(kind)` builds default params per kind. If the coder
// spells members differently, update these call sites — not the expected
// behavior (rate-independent convergence, custom same call shape).
//
// ASSUMPTION (convergence tolerance): "converges to the same distance"
// is read as: the same fling integrated to rest at 60/90/120/144Hz lands
// within 1% relative (or 1.0px absolute for near-zero flings). Mark
// ASSUMPTION per GR-08 T1.2.
#include <cmath>
#include <cstdio>
#include <vector>

#include "lists/scroll_physics.h"
#include "lists/stable_key.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::CurveParams mkCurve(lists::CurveKind kind) {
  lists::CurveParams c{};
  c.kind = kind;
  c.stiffness = 180.0f;
  c.damping = 18.0f;
  c.friction = 0.35f;
  c.flingMultiplier = 1.0f;
  c.rubberBand = 0.45f;
  return c;
}

// Integrate the identical fling (initial velocity, no touch input) to rest
// at a fixed frame step, returning total distance travelled.
float flingDistance(float initialVelocityPxS, float dtS,
                    lists::CurveParams curve) {
  lists::PhysicsState s{};
  s.offsetPx = 0.0f;
  s.velocityPxS = initialVelocityPxS;
  lists::PredictedSample sample{};
  sample.offsetPx = 0.0f;
  sample.velocityPxS = 0.0f;  // finger lifted: no driving touch input
  lists::VsyncDeadline vsync{};
  vsync.dtS = dtS;
  for (int i = 0; i < 3600; ++i) {
    s = lists::integrate(s, sample, vsync, curve);
    if (std::fabs(s.velocityPxS) < 0.5f) break;
  }
  return s.offsetPx;
}

bool nearRel(float a, float b, float relTol, float absTol) {
  float diff = std::fabs(a - b);
  if (diff <= absTol) return true;
  float mag = std::fmax(std::fabs(a), std::fabs(b));
  return mag > 0.0f && diff / mag <= relTol;
}

}  // namespace

int main() {
  // Consumes batch-1 T1: keys under test stay well-formed; proves the
  // tester builds on staged headers without redefining them.
  {
    std::vector<lists::ItemKey> keys(3);
    keys[0].key = "a";
    keys[1].key = "b";
    keys[2].key = "c";
    lists::KeyCheck kc = lists::checkKeys(keys, false);
    check(kc.ok, "t1_keys_still_pass");
  }

  const float kFling = 4000.0f;
  const float kDt60 = 1.0f / 60.0f;
  const float kDt90 = 1.0f / 90.0f;
  const float kDt120 = 1.0f / 120.0f;
  const float kDt144 = 1.0f / 144.0f;

  // AC-LISTS-008: identical fling at 60/90/120/144Hz converges to the same
  // distance (android-overscroll curve).
  {
    lists::CurveParams curve = mkCurve(lists::CurveKind::AndroidOverscroll);
    float d60 = flingDistance(kFling, kDt60, curve);
    float d90 = flingDistance(kFling, kDt90, curve);
    float d120 = flingDistance(kFling, kDt120, curve);
    float d144 = flingDistance(kFling, kDt144, curve);
    check(d60 > 0.0f, "overscroll_fling_moves_forward");
    check(nearRel(d90, d60, 0.01f, 1.0f), "overscroll_90_matches_60");
    check(nearRel(d120, d60, 0.01f, 1.0f), "overscroll_120_matches_60");
    check(nearRel(d144, d60, 0.01f, 1.0f), "overscroll_144_matches_60");
  }

  // Same rate-independence holds for the spring-bounce curve.
  {
    lists::CurveParams curve = mkCurve(lists::CurveKind::SpringBounce);
    float d60 = flingDistance(kFling, kDt60, curve);
    float d120 = flingDistance(kFling, kDt120, curve);
    float d144 = flingDistance(kFling, kDt144, curve);
    check(nearRel(d120, d60, 0.01f, 1.0f), "spring_120_matches_60");
    check(nearRel(d144, d60, 0.01f, 1.0f), "spring_144_matches_60");
  }

  // Custom curve runs under the same call shape and converges across rates.
  {
    lists::CurveParams curve = mkCurve(lists::CurveKind::Custom);
    curve.stiffness = 260.0f;
    curve.damping = 24.0f;
    float d60 = flingDistance(kFling, kDt60, curve);
    float d144 = flingDistance(kFling, kDt144, curve);
    check(d60 > 0.0f, "custom_fling_moves_forward");
    check(nearRel(d144, d60, 0.01f, 1.0f), "custom_144_matches_60");
  }

  // Boundary: rest state stays at rest (no spontaneous motion).
  {
    lists::PhysicsState s{};
    s.offsetPx = 100.0f;
    s.velocityPxS = 0.0f;
    lists::PredictedSample sample{};
    sample.offsetPx = 0.0f;
    sample.velocityPxS = 0.0f;
    lists::VsyncDeadline vsync{};
    vsync.dtS = kDt60;
    lists::PhysicsState out =
        lists::integrate(s, sample, vsync, mkCurve(lists::CurveKind::Custom));
    check(std::fabs(out.offsetPx - 100.0f) <= 1.0f, "rest_stays_at_rest");
    check(std::fabs(out.velocityPxS) <= 1.0f, "rest_gains_no_velocity");
  }

  // Boundary: zero-dt frame is a no-op, never NaN/divergent.
  {
    lists::PhysicsState s{};
    s.offsetPx = 50.0f;
    s.velocityPxS = 2000.0f;
    lists::PredictedSample sample{};
    lists::VsyncDeadline vsync{};
    vsync.dtS = 0.0f;
    lists::PhysicsState out = lists::integrate(
        s, sample, vsync, mkCurve(lists::CurveKind::AndroidOverscroll));
    check(out.offsetPx == out.offsetPx && out.velocityPxS == out.velocityPxS,
          "zero_dt_no_nan");
    check(std::fabs(out.offsetPx - 50.0f) <= 1.0f, "zero_dt_no_jump");
  }

  if (g_failures == 0) std::printf("ALL TESTS PASSED\n");
  return g_failures == 0 ? 0 : 1;
}
