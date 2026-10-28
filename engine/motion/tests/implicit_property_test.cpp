// T6a contract tests — FR-MOTION-007 / AC-MOTION-014, AC-MOTION-015
// (TC-MOTION-014): marked property animates with zero imperative code;
// unmarked property snaps; get on unregistered property returns last set
// value with no throw.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: explicit
// tMs per call, no wall clock, no randomness.
// Binds to the T6 `Interface:` line (reproduced verbatim in unit-tests/T6.md).
//
// ASSUMPTION (TransitionSpec/PropertyId shapes): the plan names
// `TransitionSpec{SpringSpec or Easing, delayMs, staggerMs}` reusing the T1
// `motion::SpringSpec` naming (motion/solver.h), but leaves the exact shape
// open. These tests use `motion::PropertyId{std::uint32_t value}` (mirroring
// the T2/T4 NodeId convention) and `motion::TransitionSpec{SpringSpec
// spring; float delayMs; float staggerMs;}` with a spring-only variant; an
// easing variant reconciles later without changing these expectations.
// Field/type spellings are the test's proposal — update call sites, not
// expectations, if the coder names them differently.
//
// ASSUMPTION (velocity units): plan A3 states retarget epsilon 1e-3 units/ms;
// T1 solver.h documents velocity in units per second. These tests assert
// value continuity only (|d value| <= 1e-3) and convergence, never raw
// velocity units.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "motion/implicit.h"
#include "motion/solver.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

const float kValueEps = 1e-3f;

motion::PropertyId pid(std::uint32_t v) {
  motion::PropertyId p;
  p.value = v;
  return p;
}

motion::TransitionSpec springSpec(float stiffness, float dampingRatio) {
  motion::TransitionSpec s;
  s.spring.stiffness = stiffness;
  s.spring.dampingRatio = dampingRatio;
  s.delayMs = 0.0f;
  s.staggerMs = 0.0f;
  return s;
}

// Marked property animates: immediately after set, get still reads the old
// value (no snap), and with zero further caller code it converges to target.
bool check_marked_property_animates_without_imperative_code() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(1), springSpec(180.0f, 1.0f));
  reg.set(pid(1), 0.0f, 0.0f);
  reg.set(pid(1), 100.0f, 0.0f);
  float atSet = reg.get(pid(1), 0.0f);
  if (!near(atSet, 0.0f, kValueEps)) return false;  // must not snap
  float settled = reg.get(pid(1), 5000.0f);
  return near(settled, 100.0f, 0.5f);  // converges with no further calls
}

// Marked property makes progress monotonically toward target over time.
bool check_marked_property_progresses_over_time() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(2), springSpec(180.0f, 1.0f));
  reg.set(pid(2), 0.0f, 0.0f);
  reg.set(pid(2), 100.0f, 0.0f);
  float early = reg.get(pid(2), 16.0f);
  float late = reg.get(pid(2), 2000.0f);
  return early > 0.0f && early < 100.0f && late > early;
}

// Unmarked property snaps: get returns the new target immediately.
bool check_unmarked_property_snaps() {
  motion::ImplicitRegistry reg;
  reg.set(pid(3), 10.0f, 0.0f);
  reg.set(pid(3), 77.0f, 100.0f);
  return reg.get(pid(3), 100.0f) == 77.0f;
}

// get on a never-set, unregistered property does not throw and returns a
// stable value (last set value semantics: default before any set).
bool check_get_unregistered_is_stable_no_throw() {
  motion::ImplicitRegistry reg;
  float a = reg.get(pid(99), 0.0f);
  float b = reg.get(pid(99), 1000.0f);
  return a == b;
}

// get on unregistered-but-set property returns the last set value exactly
// (no transition applied).
bool check_get_unregistered_returns_last_set_value() {
  motion::ImplicitRegistry reg;
  reg.set(pid(4), 5.0f, 0.0f);
  reg.set(pid(4), 42.0f, 50.0f);
  return reg.get(pid(4), 50.0f) == 42.0f &&
         reg.get(pid(4), 5000.0f) == 42.0f;
}

// Two properties are independent: marking one does not animate the other.
bool check_properties_are_independent() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(5), springSpec(180.0f, 1.0f));
  reg.set(pid(5), 0.0f, 0.0f);
  reg.set(pid(6), 0.0f, 0.0f);
  reg.set(pid(5), 100.0f, 0.0f);
  reg.set(pid(6), 100.0f, 0.0f);
  return near(reg.get(pid(5), 0.0f), 0.0f, kValueEps) &&
         reg.get(pid(6), 0.0f) == 100.0f;
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
  RUN_CASE(check_marked_property_animates_without_imperative_code);
  RUN_CASE(check_marked_property_progresses_over_time);
  RUN_CASE(check_unmarked_property_snaps);
  RUN_CASE(check_get_unregistered_is_stable_no_throw);
  RUN_CASE(check_get_unregistered_returns_last_set_value);
  RUN_CASE(check_properties_are_independent);
  if (failures > 0) {
    std::printf("implicit_property_test: %d failing\n", failures);
    return 1;
  }
  std::printf("implicit_property_test: all passing\n");
  return 0;
}
