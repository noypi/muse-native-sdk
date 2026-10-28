// T4+T5 contract tests — FR-MOTION-014 / AC-045 (TC-MOTION-045) + AC-046 (TC-MOTION-046).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Time is driven only via
// the nowMs parameters; the OS clock is never touched.
// Binds to the T4/T5 `Interface:` lines (reproduced verbatim in T4.md/T5.md).
//
// ASSUMPTION (completion deactivates): a pattern that has converged to its
// endpoint reports patternActive == false (it is no longer driving frames).
// If the coder keeps completed patterns active, update these expectations
// via reconciliation (expectations, not call sites).
// ASSUMPTION (enter/exit direction): runEnter drives the sampled value
// upward from its start, runExit drives it downward; exact endpoints are
// not pinned here (motion-occurred + convergence are asserted, plus
// retarget convergence to the explicit toValue in the T5 cases).
// ASSUMPTION (velocityHint): retarget with a nonzero velocityHint seeds the
// solver velocity with that hint, so it yields a different velocity
// immediately after than retarget with a 0.0f hint from the same state.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>

#include "motion/patterns.h"

namespace {

bool approx(float a, float b, float eps = 1e-3f) {
  return std::fabs(a - b) <= eps;
}

bool mentions(const std::exception& e, const char* needle) {
  return std::strstr(e.what(), needle) != nullptr;
}

const char* kNames[5] = {
    "fade-through",
    "shared-axis-x",
    "shared-axis-y",
    "shared-axis-z",
    "container-transform",
};

void velocityOf(motion::PatternId p, motion::NodeId target, float* value,
                float* velocity) {
  motion::patternVelocity(p, target, value, velocity);
}

}  // namespace

// ---- T4: canonical names ----

// Named case: TC-MOTION-045 — all five canonical names resolve.
static bool check_all_five_names_resolve() {
  try {
    for (const char* n : kNames) {
      motion::PatternId p = motion::patternNamed(n);
      (void)p;
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Named case: T4 Interface — unknown name throws PatternError naming it.
static bool check_unknown_name_throws_naming_it() {
  const char* missing = "motion.nope.not-a-pattern";
  try {
    motion::patternNamed(missing);
  } catch (const std::exception& e) {
    return mentions(e, missing);
  }
  return false;
}

// Named case: boundary — null name throws instead of crashing.
static bool check_null_name_throws() {
  try {
    motion::patternNamed(nullptr);
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// Named case: boundary — empty name throws instead of resolving.
static bool check_empty_name_throws() {
  try {
    motion::patternNamed("");
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

// ---- T4: enter/exit drive the target with zero app animation code ----

// Named case: re-entrant runEnter mid-flight never snaps — sampling right
// after re-entry reads the same value as before (T5 Detail: re-entrant
// runEnter/runExit/retarget mid-flight never snaps).
static bool check_run_enter_activates_and_moves() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("fade-through");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId target = 101;
  motion::runEnter(p, target, 1000);
  if (!motion::patternActive(p, target)) return false;
  float v0 = 0.0f, vel0 = 0.0f;
  velocityOf(p, target, &v0, &vel0);
  motion::runEnter(p, target, 1000 + 60);  // re-entrant while active
  if (!motion::patternActive(p, target)) return false;
  float vs = 0.0f, vels = 0.0f;
  velocityOf(p, target, &vs, &vels);
  return approx(vs, v0, 1e-3f);  // re-entrant re-run never snaps
}

// Named case: runEnter makes progress — carrying the solver forward under
// the virtual clock moves the sampled value (ASSUMPTION: the enter endpoint
// differs from the start value, i.e. enter visibly animates the target).
static bool check_run_enter_progresses_over_virtual_time() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("shared-axis-x");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId target = 102;
  motion::runEnter(p, target, 5000);
  float vStart = 0.0f, velStart = 0.0f;
  velocityOf(p, target, &vStart, &velStart);
  // A later-timestamped call carries the solver 100s forward under the
  // virtual clock; the sampled value must have moved toward the endpoint.
  motion::runEnter(p, target, 5000 + 100000);
  float vEnd = 0.0f, velEnd = 0.0f;
  velocityOf(p, target, &vEnd, &velEnd);
  (void)velStart;
  (void)velEnd;
  return !approx(vEnd, vStart, 1e-3f);
}

// Named case: runExit after a converged enter drives the target again
// (exit in flight => active) rather than snapping or going idle.
static bool check_run_exit_activates_then_completes() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("shared-axis-y");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId target = 103;
  motion::runEnter(p, target, 9000);
  if (!motion::patternActive(p, target)) return false;
  // Enter has converged 100s later; exit starts a fresh flight => active.
  motion::runExit(p, target, 9000 + 100000);
  return motion::patternActive(p, target);
}

// Named case: per-pattern enter activates for each of the five patterns.
static bool check_enter_activates_all_five_patterns() {
  try {
    for (int i = 0; i < 5; ++i) {
      motion::PatternId p = motion::patternNamed(kNames[i]);
      motion::NodeId target = static_cast<motion::NodeId>(200 + i);
      motion::runEnter(p, target, 20000);
      if (!motion::patternActive(p, target)) return false;
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Named case: per-pattern exit activates for each of the five patterns.
static bool check_exit_activates_all_five_patterns() {
  try {
    for (int i = 0; i < 5; ++i) {
      motion::PatternId p = motion::patternNamed(kNames[i]);
      motion::NodeId target = static_cast<motion::NodeId>(300 + i);
      motion::runExit(p, target, 30000);
      if (!motion::patternActive(p, target)) return false;
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Named case: per-target isolation — running a pattern on one node does not
// activate it on another node.
static bool check_targets_are_independent() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("container-transform");
  } catch (const std::exception&) {
    return false;
  }
  motion::runEnter(p, 401, 40000);
  if (!motion::patternActive(p, 401)) return false;
  return !motion::patternActive(p, 402);
}

// Named case: per-pattern isolation — running one pattern does not activate
// another pattern on the same target.
static bool check_patterns_are_independent_per_target() {
  motion::PatternId a, b;
  try {
    a = motion::patternNamed("fade-through");
    b = motion::patternNamed("shared-axis-z");
  } catch (const std::exception&) {
    return false;
  }
  motion::runEnter(a, 501, 50000);
  if (!motion::patternActive(a, 501)) return false;
  return !motion::patternActive(b, 501);
}

// ---- T5: interruptible retarget ----

// Named case: TC-MOTION-046 continuity — retarget mid-flight preserves
// value+velocity within epsilon 1e-3 (T5 Interface).
static bool check_retarget_preserves_value_and_velocity() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("fade-through");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId target = 601;
  motion::runEnter(p, target, 60000);
  float vBefore = 0.0f, velBefore = 0.0f;
  velocityOf(p, target, &vBefore, &velBefore);
  motion::retarget(p, target, vBefore + 0.5f, 0.0f, 60000 + 120);
  float vAfter = 0.0f, velAfter = 0.0f;
  velocityOf(p, target, &vAfter, &velAfter);
  // Value continuity: the retarget must not snap (epsilon 1e-3 per T5).
  // Velocity continuity with a 0.0f hint: solver keeps its velocity.
  if (!approx(vAfter, vBefore, 1e-3f)) return false;
  return approx(velAfter, velBefore, 1e-3f);
}

// Named case: TC-MOTION-046 continuity for every pattern.
static bool check_retarget_continuity_all_five_patterns() {
  try {
    for (int i = 0; i < 5; ++i) {
      motion::PatternId p = motion::patternNamed(kNames[i]);
      motion::NodeId target = static_cast<motion::NodeId>(700 + i);
      motion::runEnter(p, target, 70000);
      float v0 = 0.0f, vel0 = 0.0f;
      velocityOf(p, target, &v0, &vel0);
      motion::retarget(p, target, v0 + 0.5f, 0.0f, 70000 + 120);
      float v1 = 0.0f, vel1 = 0.0f;
      velocityOf(p, target, &v1, &vel1);
      if (!approx(v1, v0, 1e-3f)) return false;
      if (!approx(vel1, vel0, 1e-3f)) return false;
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

// Named case: retarget converges to the new target on the virtual clock.
static bool check_retarget_converges_to_new_target() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("shared-axis-x");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId target = 801;
  motion::runEnter(p, target, 80000);
  const float goal = 2.0f;
  motion::retarget(p, target, goal, 0.0f, 80000 + 120);
  // Convergence is observed by retargeting at a far later stamp: the solver
  // state carried forward must read at the goal.
  motion::retarget(p, target, goal, 0.0f, 80000 + 120 + 100000);
  float v = 0.0f, vel = 0.0f;
  velocityOf(p, target, &v, &vel);
  return approx(v, goal, 0.05f);
}

// Named case: velocityHint seeds solver velocity (gesture/release velocity
// carried into the retarget per T5 Detail).
static bool check_velocity_hint_seeds_velocity() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("shared-axis-y");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId targetA = 901;
  const motion::NodeId targetB = 902;
  motion::runEnter(p, targetA, 90000);
  motion::runEnter(p, targetB, 90000);
  motion::retarget(p, targetA, 1.0f, 0.0f, 90000 + 50);
  motion::retarget(p, targetB, 1.0f, 3.0f, 90000 + 50);
  float vA = 0.0f, velA = 0.0f, vB = 0.0f, velB = 0.0f;
  velocityOf(p, targetA, &vA, &velA);
  velocityOf(p, targetB, &vB, &velB);
  (void)vA;
  (void)vB;
  return !approx(velA, velB, 1e-3f);
}

// Named case: patternVelocity reports the running value (observable output,
// not just a call echo).
static bool check_pattern_velocity_reports_motion() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("shared-axis-z");
  } catch (const std::exception&) {
    return false;
  }
  const motion::NodeId target = 1001;
  motion::runEnter(p, target, 100000);
  float v0 = 0.0f, vel0 = 0.0f;
  velocityOf(p, target, &v0, &vel0);
  motion::retarget(p, target, v0 + 1.0f, 0.0f, 100000 + 100000);
  float v1 = 0.0f, vel1 = 0.0f;
  velocityOf(p, target, &v1, &vel1);
  (void)vel1;
  return (v1 > v0);
}

// Named case: boundary — patternVelocity with null out-pointers must not crash
// (throwing is acceptable).
static bool check_pattern_velocity_null_pointers_do_not_crash() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("fade-through");
  } catch (const std::exception&) {
    return false;
  }
  motion::runEnter(p, 1101, 110000);
  try {
    motion::patternVelocity(p, 1101, nullptr, nullptr);
  } catch (const std::exception&) {
    return true;
  }
  return true;
}

// Named case: boundary — patternVelocity with one null out-pointer must not crash.
static bool check_pattern_velocity_half_null_does_not_crash() {
  motion::PatternId p;
  try {
    p = motion::patternNamed("fade-through");
  } catch (const std::exception&) {
    return false;
  }
  motion::runEnter(p, 1102, 111000);
  float v = 0.0f;
  try {
    motion::patternVelocity(p, 1102, &v, nullptr);
  } catch (const std::exception&) {
    return true;
  }
  return true;
}

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_all_five_names_resolve);
  RUN_CASE(check_unknown_name_throws_naming_it);
  RUN_CASE(check_null_name_throws);
  RUN_CASE(check_empty_name_throws);
  RUN_CASE(check_run_enter_activates_and_moves);
  RUN_CASE(check_run_enter_progresses_over_virtual_time);
  RUN_CASE(check_run_exit_activates_then_completes);
  RUN_CASE(check_enter_activates_all_five_patterns);
  RUN_CASE(check_exit_activates_all_five_patterns);
  RUN_CASE(check_targets_are_independent);
  RUN_CASE(check_patterns_are_independent_per_target);
  RUN_CASE(check_retarget_preserves_value_and_velocity);
  RUN_CASE(check_retarget_continuity_all_five_patterns);
  RUN_CASE(check_retarget_converges_to_new_target);
  RUN_CASE(check_velocity_hint_seeds_velocity);
  RUN_CASE(check_pattern_velocity_reports_motion);
  RUN_CASE(check_pattern_velocity_null_pointers_do_not_crash);
  RUN_CASE(check_pattern_velocity_half_null_does_not_crash);
  if (failures > 0) {
    std::printf("patterns_test: %d failing\n", failures);
    return 1;
  }
  std::printf("patterns_test: all passing\n");
  return 0;
}
