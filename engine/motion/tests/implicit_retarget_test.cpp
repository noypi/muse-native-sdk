// T6b contract tests — FR-MOTION-007 / AC-MOTION-014, AC-MOTION-015
// (TC-MOTION-015): in-flight implicit transition interrupted by a new set
// retargets via the T1 solver from the current value+velocity with
// continuity within epsilon at the retarget step.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: explicit
// tMs per call, no wall clock, no randomness.
// Binds to the T6 `Interface:` line (reproduced verbatim in unit-tests/T6.md).
//
// ASSUMPTION (TransitionSpec/PropertyId shapes): same as T6a — see
// implicit_property_test.cpp header. `TransitionSpec{SpringSpec spring;
// float delayMs; float staggerMs;}` reusing T1 `motion::SpringSpec`
// (motion/solver.h); spring-only variant. Update call sites, not
// expectations, if the coder names fields differently.
//
// ASSUMPTION (velocity units): canonical unit is units/s everywhere
// (gesture_binding.h, solver.h); retarget epsilon 1e-3 units/s. Value continuity is
// asserted directly (|d value| <= 1e-3 across the retarget step); velocity
// continuity is asserted observationally (post-retarget trajectory stays
// within a bounded step of the pre-retarget value, i.e. no snap or kick),
// never in raw velocity units.
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

// Interrupted transition shows value continuity within epsilon at retarget:
// get just after the second set matches the in-flight value just before it.
bool check_retarget_value_continuity_within_epsilon() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(1), springSpec(180.0f, 1.0f));
  reg.set(pid(1), 0.0f, 0.0f);
  reg.set(pid(1), 100.0f, 0.0f);
  float before = reg.get(pid(1), 120.0f);  // mid-flight value
  reg.set(pid(1), 200.0f, 120.0f);         // interrupt at same tMs
  float after = reg.get(pid(1), 120.0f);
  return near(after, before, kValueEps);
}

// No snap one small step after retarget: trajectory continues smoothly from
// the retarget point instead of jumping toward either endpoint.
bool check_no_snap_one_step_after_retarget() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(2), springSpec(180.0f, 1.0f));
  reg.set(pid(2), 0.0f, 0.0f);
  reg.set(pid(2), 100.0f, 0.0f);
  float before = reg.get(pid(2), 120.0f);
  reg.set(pid(2), -100.0f, 120.0f);  // hard reversal
  float stepped = reg.get(pid(2), 121.0f);
  return near(stepped, before, 5.0f);  // bounded step, no snap to target
}

// Retargeted transition converges to the NEW target, not the old one.
bool check_retarget_converges_to_new_target() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(3), springSpec(180.0f, 1.0f));
  reg.set(pid(3), 0.0f, 0.0f);
  reg.set(pid(3), 100.0f, 0.0f);
  (void)reg.get(pid(3), 120.0f);     // advance mid-flight
  reg.set(pid(3), 50.0f, 120.0f);    // interrupt
  float settled = reg.get(pid(3), 6000.0f);
  return near(settled, 50.0f, 0.5f);
}

// Double interrupt: two rapid retargets each preserve continuity and the
// final target still wins.
bool check_double_interrupt_continuity_and_final_target() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(4), springSpec(180.0f, 1.0f));
  reg.set(pid(4), 0.0f, 0.0f);
  reg.set(pid(4), 100.0f, 0.0f);
  float v1 = reg.get(pid(4), 60.0f);
  reg.set(pid(4), 200.0f, 60.0f);
  if (!near(reg.get(pid(4), 60.0f), v1, kValueEps)) return false;
  float v2 = reg.get(pid(4), 90.0f);
  reg.set(pid(4), 300.0f, 90.0f);
  if (!near(reg.get(pid(4), 90.0f), v2, kValueEps)) return false;
  return near(reg.get(pid(4), 6000.0f), 300.0f, 0.5f);
}

// Retarget to the current value holds steady (no induced motion).
bool check_retarget_to_current_value_holds() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(5), springSpec(180.0f, 1.0f));
  reg.set(pid(5), 0.0f, 0.0f);
  reg.set(pid(5), 100.0f, 0.0f);
  float mid = reg.get(pid(5), 120.0f);
  reg.set(pid(5), mid, 120.0f);
  float held = reg.get(pid(5), 3000.0f);
  return near(held, mid, 0.5f);
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
  RUN_CASE(check_retarget_value_continuity_within_epsilon);
  RUN_CASE(check_no_snap_one_step_after_retarget);
  RUN_CASE(check_retarget_converges_to_new_target);
  RUN_CASE(check_double_interrupt_continuity_and_final_target);
  RUN_CASE(check_retarget_to_current_value_holds);
  if (failures > 0) {
    std::printf("implicit_retarget_test: %d failing\n", failures);
    return 1;
  }
  std::printf("implicit_retarget_test: all passing\n");
  return 0;
}
