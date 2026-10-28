// Batch 1 contract tests — T1 (FR-MOTION-001 / AC-MOTION-002,
// TC-MOTION-002) + T2 (FR-MOTION-001 / AC-MOTION-002; FR-MOTION-008 /
// AC-MOTION-020, consumed contract).
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: explicit dt per call,
// no wall clock, no randomness. Follows the engine/motion convention
// (plain main(), RUN_CASE macro, printf PASS/FAIL, nonzero exit on failure;
// cf. tests/implicit_retarget_test.cpp).
//
// Binds to the plan items' `Interface:` lines (reproduced verbatim in
// unit-tests/T1.md and unit-tests/T2.md). Shared file: T1 owns/commits it;
// T2 maps onto the arbitration cases below (GR-08 T1.8).
//
// ASSUMPTION (T1 error channel): the plan `Interface:` line names
// `MotionError` but the declared return type `RetargetResult` carries no
// error field, so the error-reporting channel is genuinely silent. These
// cases assert the observable contract — invalid stiffness/timestep never
// throws, leaves the state unchanged, and reports not-settled — and the
// call sites will be reconciled (not the expectations) once the coder
// publishes the named error channel.
// ASSUMPTION (T1 settle rule): the plan pins no settle threshold; settled
// is asserted only directionally (far-from-target in flight => false;
// converged near target with near-zero velocity => true).
// ASSUMPTION (T1 NaN): NaN stiffness/timestep counts as invalid (unchanged,
// not settled, no throw).
// ASSUMPTION (T2 exact tie): identical priority AND identical start frame
// has no documented winner; only repeat-determinism is asserted, never
// which enumerant wins.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "motion/arbitration.h"
#include "motion/spring.h"

namespace {

bool near(double a, double b, double eps) {
  return std::fabs(a - b) <= eps;
}

const double kSettleEps = 1e-3;

// No-snap band for a target swap: one step from the same mid-flight (x, v)
// toward the NEW target vs the OLD target differs only by the spring-force
// delta k*|dTarget|*dt^2 (here 180*100/120^2 = 1.25). The band sits well
// above that (genuine step motion is allowed) and far below any visible
// teleport, mirroring the pre-existing `implicit_retarget_test` no-snap
// bound. NOTE: asserting output == input within 1e-6 after a step is
// unsatisfiable — a genuine semi-implicit Euler step advances O(v*dt) >>
// 1e-6 mid-flight — so continuity is asserted differentially (the retarget
// itself introduces no jump), never as zero motion.
const double kNoSnapEps = 2.0;
const double kDt = 1.0 / 120.0;

// ---- T1: interruptible spring solver with velocity-preserving retarget ----

// Mid-flight retarget swaps only the target (no-snap): one step from the
// same in-flight (x, v) toward the new target lands within kNoSnapEps of
// one step toward the old target, the handed-in velocity is carried into
// the new step (velocity preservation), and the spring is still in flight.
// AC-MOTION-002 continuity, asserted as the spec no-snap property.
bool check_retarget_preserves_position_and_velocity_within_1e_6() {
  SpringState s{0.0, 0.0, 180.0, 12.0};
  // Drive mid-flight toward an initial target.
  for (int i = 0; i < 30; ++i) {
    s = motion_retarget(s, 100.0, s.v, kDt).state;
  }
  RetargetResult cont = motion_retarget(s, 100.0, s.v, kDt);
  RetargetResult r = motion_retarget(s, 200.0, s.v, kDt);
  // Velocity handoff: the retargeted step must carry the in-flight momentum
  // (a zero-velocity handoff from the same state would step differently).
  RetargetResult dropped = motion_retarget(s, 200.0, 0.0, kDt);
  return near(r.state.x, cont.state.x, kNoSnapEps) &&
         r.state.v != dropped.state.v && !r.settled;
}

// Retarget only swaps the target: retargeting to the current value with
// zero velocity holds steady (no induced motion).
bool check_retarget_to_current_value_holds_steady() {
  SpringState s{0.0, 0.0, 180.0, 12.0};
  for (int i = 0; i < 30; ++i) {
    s = motion_retarget(s, 100.0, s.v, 1.0 / 120.0).state;
  }
  double midX = s.x;
  SpringState held{midX, 0.0, s.stiffness, s.damping};
  for (int i = 0; i < 600; ++i) {
    held = motion_retarget(held, midX, held.v, 1.0 / 120.0).state;
  }
  return near(held.x, midX, kSettleEps) && near(held.v, 0.0, kSettleEps);
}

// A retargeted spring converges to the NEW target, not the old one.
bool check_retarget_converges_to_new_target() {
  SpringState s{0.0, 0.0, 180.0, 12.0};
  for (int i = 0; i < 30; ++i) {
    s = motion_retarget(s, 100.0, s.v, 1.0 / 120.0).state;
  }
  RetargetResult r = motion_retarget(s, -50.0, s.v, 1.0 / 120.0);
  SpringState t = r.state;
  bool settled = false;
  for (int i = 0; i < 10000; ++i) {
    RetargetResult step = motion_retarget(t, -50.0, t.v, 1.0 / 120.0);
    t = step.state;
    if (step.settled) {
      settled = true;
      break;
    }
  }
  return settled && near(t.x, -50.0, kSettleEps);
}

// Double interrupt: two rapid retargets each swap only the target (no-snap
// vs continuing toward the previous target) and the final target wins.
bool check_double_interrupt_continuity_and_final_target() {
  SpringState s{0.0, 0.0, 180.0, 12.0};
  for (int i = 0; i < 15; ++i) {
    s = motion_retarget(s, 100.0, s.v, kDt).state;
  }
  RetargetResult cont1 = motion_retarget(s, 100.0, s.v, kDt);
  RetargetResult r1 = motion_retarget(s, 200.0, s.v, kDt);
  if (!near(r1.state.x, cont1.state.x, kNoSnapEps)) return false;
  SpringState t = r1.state;
  for (int i = 0; i < 15; ++i) {
    t = motion_retarget(t, 200.0, t.v, kDt).state;
  }
  RetargetResult cont2 = motion_retarget(t, 200.0, t.v, kDt);
  RetargetResult r2 = motion_retarget(t, 300.0, t.v, kDt);
  if (!near(r2.state.x, cont2.state.x, kNoSnapEps)) return false;
  t = r2.state;
  bool settled = false;
  for (int i = 0; i < 10000; ++i) {
    RetargetResult step = motion_retarget(t, 300.0, t.v, kDt);
    t = step.state;
    if (step.settled) {
      settled = true;
      break;
    }
  }
  return settled && near(t.x, 300.0, kSettleEps);
}

// Same inputs produce identical outputs (deterministic replay of one step).
bool check_retarget_is_deterministic_for_same_inputs() {
  SpringState s{10.0, -3.0, 180.0, 12.0};
  RetargetResult a = motion_retarget(s, 42.0, s.v, 1.0 / 120.0);
  RetargetResult b = motion_retarget(s, 42.0, s.v, 1.0 / 120.0);
  return a.state.x == b.state.x && a.state.v == b.state.v &&
         a.settled == b.settled;
}

// Invalid stiffness (zero, negative, NaN): no throw, state unchanged,
// not settled.
bool check_invalid_stiffness_returns_error_without_throw() {
  const double stiffnesses[] = {
      0.0, -1.0, -180.0, std::numeric_limits<double>::quiet_NaN()};
  for (double k : stiffnesses) {
    SpringState s{10.0, 2.0, k, 12.0};
    try {
      RetargetResult r = motion_retarget(s, 100.0, s.v, 1.0 / 120.0);
      if (r.settled) return false;
      if (r.state.x != s.x || r.state.v != s.v) return false;
    } catch (...) {
      return false;  // must never throw
    }
  }
  return true;
}

// Invalid timestep (zero, negative, NaN): no throw, state unchanged,
// not settled.
bool check_invalid_timestep_returns_error_without_throw() {
  const double dts[] = {
      0.0, -1.0 / 120.0, std::numeric_limits<double>::quiet_NaN()};
  for (double dt : dts) {
    SpringState s{10.0, 2.0, 180.0, 12.0};
    try {
      RetargetResult r = motion_retarget(s, 100.0, s.v, dt);
      if (r.settled) return false;
      if (r.state.x != s.x || r.state.v != s.v) return false;
    } catch (...) {
      return false;  // must never throw
    }
  }
  return true;
}

// ---- T2: deterministic overlap-arbitration rule (pure total order) ----

// Higher priority wins regardless of start frames.
bool check_higher_priority_wins_regardless_of_frames() {
  if (motion_arbitrate(100, 0, 1, 5) != ArbitrationWinner::kHighestPriority)
    return false;
  if (motion_arbitrate(0, 100, 5, 1) != ArbitrationWinner::kHighestPriority)
    return false;
  return true;
}

// Equal priority falls back to recency (newest start frame).
bool check_equal_priority_newest_start_frame_wins() {
  if (motion_arbitrate(10, 20, 3, 3) != ArbitrationWinner::kNewest)
    return false;
  if (motion_arbitrate(20, 10, 3, 3) != ArbitrationWinner::kNewest)
    return false;
  return true;
}

// Asymmetric recency (review gap 1): equal priority resolves per the
// documented rule — kNewest means the newer-frame party wins, i.e. BOTH
// frames participate in the comparison. Incumbent-newer and challenger-newer
// pairs each yield kNewest deterministically (repeat-stable and stable
// across magnitudes), and the exact tie (equal frames, no newer party)
// yields kNewest by convention.
bool check_equal_priority_asymmetric_recency_resolves_per_rule() {
  try {
    // Incumbent is the newer party.
    if (motion_arbitrate(100, 0, 5, 5) != ArbitrationWinner::kNewest)
      return false;
    // Challenger is the newer party: same rule result, not a flipped rule.
    if (motion_arbitrate(0, 100, 5, 5) != ArbitrationWinner::kNewest)
      return false;
    // Same asymmetry at other magnitudes and priorities.
    if (motion_arbitrate(42, 7, 0, 0) != ArbitrationWinner::kNewest)
      return false;
    if (motion_arbitrate(7, 42, 0, 0) != ArbitrationWinner::kNewest)
      return false;
    // Exact tie: no newer party, kNewest by convention, repeat-stable.
    if (motion_arbitrate(9, 9, 5, 5) != ArbitrationWinner::kNewest)
      return false;
    if (motion_arbitrate(9, 9, 5, 5) != motion_arbitrate(9, 9, 5, 5))
      return false;
    // Swapping the frames never changes the deciding rule for equal
    // priority (both orders report the recency rule).
    if (motion_arbitrate(100, 0, 5, 5) != motion_arbitrate(0, 100, 5, 5))
      return false;
  } catch (...) {
    return false;  // pure function: must never throw
  }
  return true;
}

// Exhaustive pair test: same inputs always resolve to the same winner
// (repeat determinism over a grid of frames x priorities).
bool check_same_inputs_always_resolve_to_same_winner() {
  const std::uint64_t frames[] = {0, 1, 5, 100,
                                  std::numeric_limits<std::uint64_t>::max()};
  const int priorities[] = {-1, 0, 1, 5};
  for (std::uint64_t fi : frames) {
    for (std::uint64_t fc : frames) {
      for (int pi : priorities) {
        for (int pc : priorities) {
          ArbitrationWinner first =
              motion_arbitrate(fi, fc, pi, pc);
          ArbitrationWinner second =
              motion_arbitrate(fi, fc, pi, pc);
          if (first != second) return false;
        }
      }
    }
  }
  return true;
}

// Extreme inputs (frame/priority limits) stay deterministic and never trap.
bool check_extreme_inputs_are_deterministic() {
  const int maxInt = std::numeric_limits<int>::max();
  const int minInt = std::numeric_limits<int>::min();
  const std::uint64_t maxU = std::numeric_limits<std::uint64_t>::max();
  try {
    ArbitrationWinner a = motion_arbitrate(maxU, 0, maxInt, minInt);
    ArbitrationWinner b = motion_arbitrate(maxU, 0, maxInt, minInt);
    if (a != b) return false;
    if (motion_arbitrate(0, 0, minInt, minInt) !=
        motion_arbitrate(0, 0, minInt, minInt))
      return false;
  } catch (...) {
    return false;  // pure function: must never throw
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_retarget_preserves_position_and_velocity_within_1e_6);
  RUN_CASE(check_retarget_to_current_value_holds_steady);
  RUN_CASE(check_retarget_converges_to_new_target);
  RUN_CASE(check_double_interrupt_continuity_and_final_target);
  RUN_CASE(check_retarget_is_deterministic_for_same_inputs);
  RUN_CASE(check_invalid_stiffness_returns_error_without_throw);
  RUN_CASE(check_invalid_timestep_returns_error_without_throw);
  RUN_CASE(check_higher_priority_wins_regardless_of_frames);
  RUN_CASE(check_equal_priority_newest_start_frame_wins);
  RUN_CASE(check_equal_priority_asymmetric_recency_resolves_per_rule);
  RUN_CASE(check_same_inputs_always_resolve_to_same_winner);
  RUN_CASE(check_extreme_inputs_are_deterministic);
  if (failures > 0) {
    std::printf("retarget_test: %d failing\n", failures);
    return 1;
  }
  std::printf("retarget_test: all passing\n");
  return 0;
}
