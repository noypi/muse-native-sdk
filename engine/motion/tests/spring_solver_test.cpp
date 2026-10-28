// T1 contract tests — FR-MOTION-008 / AC-MOTION-018.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: explicit
// dtMs injected per step, no wall clock, no randomness.
// Binds to the T1 `Interface:` line (reproduced verbatim in unit-tests/T1.md).
//
// ASSUMPTION (M3 settle literals): source is the published Material 3 spring
// spec for the standard expressive spatial spring (stiffness ~ 1200, damping
// ratio ~ 1.0 family); the test hardcodes SETTLE_VALUE_EPS = 0.5 (px-equivalent
// units) and SETTLE_VEL_EPS = 0.5 units/s with TIMEOUT_MS = 2000 as the
// "M3 literal tolerance" proxy. If the coder pins tighter M3 constants, these
// literals tighten via contract reconciliation (expectations unchanged).
//
// ASSUMPTION (struct shapes): the plan Detail names
// `SpringState{value,velocity}` + `SpringSpec{stiffness,dampingRatio}` with
// float fields in `motion::` namespace. Field/type spellings below are the
// test's proposal; the T1 contract records the reconciliation rule (update
// call sites, not expectations) if the coder names them differently.
#include <cmath>
#include <cstdio>

#include "motion/solver.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

const float kRetargetPosEps = 1e-3f;
const float kRetargetVelEps = 1e-3f;  // units/ms per plan acceptance
const float kSettleValueEps = 0.5f;   // ASSUMPTION: M3 literal proxy
const float kSettleVelEps = 0.5f;     // ASSUMPTION: M3 literal proxy

motion::SpringSpec makeSpec() {
  motion::SpringSpec s;
  s.stiffness = 180.0f;
  s.dampingRatio = 1.0f;
  return s;
}

// Advance moves value toward target with no sign flip on first step.
bool check_advance_moves_toward_target() {
  motion::SpringState st;
  st.value = 0.0f;
  st.velocity = 0.0f;
  motion::SpringState next =
      motion::advanceSpring(st, makeSpec(), 100.0f, 16.0f);
  return next.value > st.value;
}

// Same inputs produce identical outputs (mocked clock determinism).
bool check_advance_is_deterministic() {
  motion::SpringState st;
  st.value = 10.0f;
  st.velocity = 5.0f;
  motion::SpringState a = motion::advanceSpring(st, makeSpec(), 100.0f, 8.0f);
  motion::SpringState b = motion::advanceSpring(st, makeSpec(), 100.0f, 8.0f);
  return a.value == b.value && a.velocity == b.velocity;
}

// Retarget preserves value+velocity within epsilon across the retarget step.
bool check_retarget_preserves_state() {
  motion::SpringState mid;
  mid.value = 42.0f;
  mid.velocity = 3.5f;
  motion::SpringState r = motion::retargetSpring(mid, 200.0f);
  return near(r.value, mid.value, kRetargetPosEps) &&
         near(r.velocity, mid.velocity, kRetargetVelEps);
}

// Retarget at arbitrary mid-flight state: advance, retarget, advance one
// small step — continuity holds across the boundary.
bool check_retarget_mid_flight_continuity() {
  motion::SpringState st;
  st.value = 0.0f;
  st.velocity = 0.0f;
  for (int i = 0; i < 10; ++i) {
    st = motion::advanceSpring(st, makeSpec(), 100.0f, 16.0f);
  }
  motion::SpringState r = motion::retargetSpring(st, 50.0f);
  if (!near(r.value, st.value, kRetargetPosEps)) return false;
  if (!near(r.velocity, st.velocity, kRetargetVelEps)) return false;
  motion::SpringState after =
      motion::advanceSpring(r, makeSpec(), 50.0f, 1.0f);
  return near(after.value, r.value, 1.0f) &&
         near(after.velocity, r.velocity, 50.0f);
}

// Invalid stiffness <= 0 yields unchanged state (documented no-throw rule).
bool check_invalid_stiffness_returns_unchanged() {
  motion::SpringState st;
  st.value = 7.0f;
  st.velocity = 2.0f;
  motion::SpringSpec bad = makeSpec();
  bad.stiffness = 0.0f;
  motion::SpringState next =
      motion::advanceSpring(st, bad, 100.0f, 16.0f);
  if (next.value != st.value || next.velocity != st.velocity) return false;
  bad.stiffness = -5.0f;
  next = motion::advanceSpring(st, bad, 100.0f, 16.0f);
  return next.value == st.value && next.velocity == st.velocity;
}

// Invalid damping <= 0 yields unchanged state (documented no-throw rule).
bool check_invalid_damping_returns_unchanged() {
  motion::SpringState st;
  st.value = 7.0f;
  st.velocity = 2.0f;
  motion::SpringSpec bad = makeSpec();
  bad.dampingRatio = 0.0f;
  motion::SpringState next =
      motion::advanceSpring(st, bad, 100.0f, 16.0f);
  if (next.value != st.value || next.velocity != st.velocity) return false;
  bad.dampingRatio = -1.0f;
  next = motion::advanceSpring(st, bad, 100.0f, 16.0f);
  return next.value == st.value && next.velocity == st.velocity;
}

// Zero dt leaves state unchanged.
bool check_zero_dt_returns_unchanged() {
  motion::SpringState st;
  st.value = 7.0f;
  st.velocity = 2.0f;
  motion::SpringState next =
      motion::advanceSpring(st, makeSpec(), 100.0f, 0.0f);
  return next.value == st.value && next.velocity == st.velocity;
}

// Critically-damped spring settles within M3 literal tolerance (ASSUMPTION).
bool check_spring_settles_within_tolerance() {
  motion::SpringState st;
  st.value = 0.0f;
  st.velocity = 0.0f;
  const float target = 100.0f;
  float t = 0.0f;
  while (t < 2000.0f) {  // ASSUMPTION: TIMEOUT_MS = 2000
    st = motion::advanceSpring(st, makeSpec(), target, 16.0f);
    t += 16.0f;
    if (near(st.value, target, kSettleValueEps) &&
        std::fabs(st.velocity) <= kSettleVelEps) {
      return true;
    }
  }
  return false;
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_advance_moves_toward_target);
  RUN_CASE(check_advance_is_deterministic);
  RUN_CASE(check_retarget_preserves_state);
  RUN_CASE(check_retarget_mid_flight_continuity);
  RUN_CASE(check_invalid_stiffness_returns_unchanged);
  RUN_CASE(check_invalid_damping_returns_unchanged);
  RUN_CASE(check_zero_dt_returns_unchanged);
  RUN_CASE(check_spring_settles_within_tolerance);
  if (failures > 0) {
    std::printf("spring_solver_test: %d failing\n", failures);
    return 1;
  }
  std::printf("spring_solver_test: all passing\n");
  return 0;
}
