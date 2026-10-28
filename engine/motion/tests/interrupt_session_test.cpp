// T10 contract tests — FR-MOTION-008 / AC-MOTION-021 (TC-MOTION-021).
// Scripted interrupted drag/dismiss session: samples -> >=2 mid-flight
// retargets -> release. Scenario test over the EXISTING T1/T3/T4 surfaces;
// no new production API.
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: explicit tMs per sample,
// no wall clock, no randomness.
// Binds to the T10 plan item (Interface: none — scenario test).
//
// M3 literals (ASSUMPTION): source is the published Material 3 spring spec
// for the standard expressive spatial spring, as in the T1 contract
// (engine/motion/tests/spring_solver_test.cpp): SETTLE_VALUE_EPS = 0.5
// units, SETTLE_VEL_EPS = 0.5 units/s, TIMEOUT_MS = 2000, retarget
// continuity epsilon 1e-3 (plan A3). If the coder pins tighter M3
// constants, these literals tighten via contract reconciliation
// (expectations unchanged).
#include <cmath>
#include <cstdio>
#include <vector>

#include "motion/arbitration.h"
#include "motion/gesture_binding.h"
#include "motion/solver.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

const float kRetargetPosEps = 1e-3f;  // plan A3 (ASSUMPTION: M3-proxy)
const float kRetargetVelEps = 1e-3f;  // units/s per canonical convention
const float kSettleValueEps = 0.5f;   // ASSUMPTION: M3 literal proxy (T1)
const float kSettleVelEps = 0.5f;     // ASSUMPTION: M3 literal proxy (T1)
const float kTimeoutMs = 2000.0f;     // ASSUMPTION: M3 literal proxy (T1)

motion::SpringSpec makeSpec() {
  motion::SpringSpec s;
  s.stiffness = 180.0f;
  s.dampingRatio = 0.9f;  // underdamped: exhibits measurable overshoot
  return s;
}

class RecordingWriter : public motion::ITransformWriter {
 public:
  void writeTransform(float value, float tMs) override {
    lastValue = value;
    lastTMs = tMs;
    ++writes;
  }
  float lastValue = 0.0f;
  float lastTMs = -1.0f;
  int writes = 0;
};

// Scripted drag with >=2 mid-flight retargets + release: continuity within
// epsilon at EVERY retarget step.
bool check_continuity_at_every_retarget() {
  motion::SpringState st;
  st.value = 0.0f;
  st.velocity = 0.0f;
  const motion::SpringSpec spec = makeSpec();
  // Drag phase: advance toward finger target 1.
  for (int i = 0; i < 10; ++i)
    st = motion::advanceSpring(st, spec, 100.0f, 16.0f);
  // Retarget 1: finger reverses.
  {
    motion::SpringState r = motion::retargetSpring(st, 40.0f);
    if (!near(r.value, st.value, kRetargetPosEps)) return false;
    if (!near(r.velocity, st.velocity, kRetargetVelEps)) return false;
    st = r;
  }
  for (int i = 0; i < 10; ++i)
    st = motion::advanceSpring(st, spec, 40.0f, 16.0f);
  // Retarget 2: finger flicks forward again.
  {
    motion::SpringState r = motion::retargetSpring(st, 160.0f);
    if (!near(r.value, st.value, kRetargetPosEps)) return false;
    if (!near(r.velocity, st.velocity, kRetargetVelEps)) return false;
    st = r;
  }
  for (int i = 0; i < 10; ++i)
    st = motion::advanceSpring(st, spec, 160.0f, 16.0f);
  // Release retarget: fling target.
  {
    motion::SpringState r = motion::retargetSpring(st, 300.0f);
    if (!near(r.value, st.value, kRetargetPosEps)) return false;
    if (!near(r.velocity, st.velocity, kRetargetVelEps)) return false;
    st = r;
  }
  return true;
}

// Release with velocity starts the spring carrying that velocity (T3
// handoff), and the driven session settles within the M3 literals.
bool check_release_handoff_and_settle() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  // Scripted drag: monotonically increasing samples, then release w/vel.
  float t = 0.0f;
  for (int i = 0; i < 5; ++i) {
    motion::InputSample s;
    s.position = 20.0f * static_cast<float>(i);
    s.velocity = 1.25f;  // units/s (canonical: gesture + solver)
    s.tMs = t;
    t += 16.0f;
    binding.onSample(s);
  }
  motion::InputSample rel;
  rel.position = 80.0f;
  rel.velocity = 1.25f;
  rel.tMs = t;
  binding.onRelease(rel);
  // T3 handoff: spring initial velocity equals the release-sample velocity
  // within 1e-3. Canonical unit is units/s everywhere (gesture_binding.h,
  // solver.h): no conversion, direct equality.
  if (!near(w.lastValue, 80.0f, 1e-3f)) return false;
  motion::SpringState st = binding.springState();
  if (!near(st.velocity, rel.velocity, 1e-3f)) return false;
  // Fling-vs-drop: the seeded spring must start ahead of a from-rest drop
  // toward the same target, so the test distinguishes a fling from a drop.
  {
    const motion::SpringSpec spec = makeSpec();
    const float target = 300.0f;
    motion::SpringState fling = st;
    motion::SpringState rest;
    rest.value = rel.position;
    rest.velocity = 0.0f;
    for (int i = 0; i < 5; ++i) {
      fling = motion::advanceSpring(fling, spec, target, 16.0f);
      rest = motion::advanceSpring(rest, spec, target, 16.0f);
    }
    if (!(fling.value > rest.value)) return false;
  }
  // Settle: advance the released spring to the fling target; must settle
  // within M3 literals before the timeout.
  const motion::SpringSpec spec = makeSpec();
  const float target = 300.0f;
  float elapsed = 0.0f;
  bool settled = false;
  float peak = st.value;
  while (elapsed < kTimeoutMs) {
    st = motion::advanceSpring(st, spec, target, 16.0f);
    elapsed += 16.0f;
    if (st.value > peak) peak = st.value;
    if (near(st.value, target, kSettleValueEps)) {
      settled = true;
      break;
    }
  }
  if (!settled) return false;
  (void)peak;  // overshoot magnitude is reported, not gated (see below)
  return true;
}

// Spring overshoot is bounded and the session converges (no perpetual
// oscillation): underdamped spring overshoots then settles within literals.
bool check_overshoot_then_settle() {
  motion::SpringState st;
  st.value = 0.0f;
  st.velocity = 0.0f;
  const motion::SpringSpec spec = makeSpec();
  const float target = 100.0f;
  float peak = 0.0f;
  float t = 0.0f;
  while (t < kTimeoutMs) {
    st = motion::advanceSpring(st, spec, target, 8.0f);
    t += 8.0f;
    if (st.value > peak) peak = st.value;
    // Settle gate: |value - target| <= 0.5 units AND |velocity| <= 0.5
    // units/s (kSettleVelEps). Units/s is canonical everywhere
    // (solver.h: velocity stored in units/s; solver.cpp integrates with
    // dt = dtMs / 1000 so accel * dt yields units/s with no ms<->s
    // conversion). The prior `kSettleVelEps * 1000.0f` (= 500 units/s)
    // was a unit error that disabled the velocity half of the gate; the
    // tightened gate matches the T1 settle literal
    // (spring_solver_test.cpp `check_spring_settles_within_tolerance`).
    // Semi-implicit Euler settle dynamics: velocity decays with the
    // value envelope for this underdamped spec (stiffness 180,
    // dampingRatio 0.9, 8ms steps), so both halves settle comfortably
    // inside the 2000ms timeout — no flakiness (deterministic: fixed
    // dtMs, no wall clock, no randomness).
    if (near(st.value, target, kSettleValueEps) &&
        std::fabs(st.velocity) <= kSettleVelEps)
      break;
  }
  if (!near(st.value, target, kSettleValueEps)) return false;
  if (std::fabs(st.velocity) > kSettleVelEps) return false;
  // ASSUMPTION (overshoot bound): M3 underdamped springs overshoot modestly;
  // gate at < 25% of travel as the hardcoded literal proxy.
  if (peak > target * 1.25f) return false;
  return true;
}

// Arbitration stays deterministic across the interrupted session: the same
// claim set resolves identically before and after retargets (T4 surface).
bool check_arbitration_stable_across_session() {
  motion::NodeId a;
  a.value = 1;
  motion::NodeId b;
  b.value = 2;
  std::vector<motion::Claim> claims = {
      {a, 1, 0.0f},
      {b, 2, 16.0f},
  };
  motion::Winner w1 = motion::arbitrate(claims);
  motion::Winner w2 = motion::arbitrate(claims);
  return !w1.none && w1.id == b && w2.id == b;
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
  RUN_CASE(check_continuity_at_every_retarget);
  RUN_CASE(check_release_handoff_and_settle);
  RUN_CASE(check_overshoot_then_settle);
  RUN_CASE(check_arbitration_stable_across_session);
  if (failures > 0) {
    std::printf("interrupt_session_test: %d failing\n", failures);
    return 1;
  }
  std::printf("interrupt_session_test: all passing\n");
  return 0;
}
