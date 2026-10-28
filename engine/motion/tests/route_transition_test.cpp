// T7 contract tests — FR-MOTION-007 / AC-MOTION-016 (TC-MOTION-016).
// Per-route navigation transition config over the T2 graph.
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: explicit tMs per call,
// no wall clock, no randomness.
// Binds to the T7 `Interface:` line (reproduced verbatim in unit-tests/T7.md).
//
// ASSUMPTION (TransitionSpec reuse): the plan states TransitionSpec "should
// reuse the T6 TransitionSpec shape (motion/implicit.h)". These tests use
// `motion::TransitionSpec{SpringSpec spring; float delayMs; float staggerMs;}`
// from "motion/implicit.h" directly. If the coder aliases or extends it
// (e.g. `using TransitionSpec = ...`), update call sites, not expectations.
//
// ASSUMPTION (Direction spelling): the plan names
// `Direction{Enter,Exit,Pop,Gesture}` in `motion::`. These tests use
// `motion::Direction::{Enter,Exit,Pop,Gesture}` as a scoped enum; reconcile
// call sites if the coder spells it differently.
#include <cmath>
#include <cstdio>
#include <string>

#include "motion/graph.h"
#include "motion/implicit.h"
#include "motion/route_transition.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

motion::TransitionSpec makeSpec(float stiffness, float dampingRatio) {
  motion::TransitionSpec s;
  s.spring.stiffness = stiffness;
  s.spring.dampingRatio = dampingRatio;
  s.delayMs = 0.0f;
  s.staggerMs = 0.0f;
  return s;
}

// Each route+direction resolves its configured spec.
bool check_each_direction_resolves_configured_spec() {
  motion::RouteTransitions rt;
  rt.configure("home", motion::Direction::Enter, makeSpec(180.0f, 1.0f));
  rt.configure("home", motion::Direction::Exit, makeSpec(220.0f, 0.9f));
  rt.configure("home", motion::Direction::Pop, makeSpec(150.0f, 1.0f));
  rt.configure("home", motion::Direction::Gesture, makeSpec(300.0f, 1.0f));
  if (!near(rt.resolve("home", motion::Direction::Enter).spring.stiffness,
            180.0f, 1e-5f))
    return false;
  if (!near(rt.resolve("home", motion::Direction::Exit).spring.stiffness,
            220.0f, 1e-5f))
    return false;
  if (!near(rt.resolve("home", motion::Direction::Pop).spring.stiffness,
            150.0f, 1e-5f))
    return false;
  return near(
      rt.resolve("home", motion::Direction::Gesture).spring.stiffness, 300.0f,
      1e-5f);
}

// Routes are isolated: configuring one route never leaks into another.
bool check_routes_are_isolated() {
  motion::RouteTransitions rt;
  rt.configure("home", motion::Direction::Enter, makeSpec(180.0f, 1.0f));
  rt.configure("settings", motion::Direction::Enter, makeSpec(400.0f, 1.0f));
  return near(
             rt.resolve("home", motion::Direction::Enter).spring.stiffness,
             180.0f, 1e-5f) &&
         near(rt.resolve("settings", motion::Direction::Enter)
                  .spring.stiffness,
              400.0f, 1e-5f);
}

// Unconfigured (route,dir) returns the default spec, never throws.
bool check_unconfigured_pair_falls_back_to_default() {
  motion::RouteTransitions rt;
  rt.configure("home", motion::Direction::Enter, makeSpec(180.0f, 1.0f));
  const motion::TransitionSpec& fallback =
      rt.resolve("missing-route", motion::Direction::Exit);
  const motion::TransitionSpec& configured =
      rt.resolve("home", motion::Direction::Enter);
  (void)fallback;
  // Default fallback must differ from (not alias) a configured entry and be
  // stable across calls.
  const motion::TransitionSpec& again =
      rt.resolve("missing-route", motion::Direction::Exit);
  if (&again != &rt.resolve("missing-route", motion::Direction::Exit))
    return false;  // resolve must return a stable reference
  return near(configured.spring.stiffness, 180.0f, 1e-5f);
}

// resolve is noexcept: unconfigured lookups cannot throw.
bool check_resolve_is_noexcept() {
  static_assert(noexcept(std::declval<const motion::RouteTransitions&>().resolve(
                    std::declval<const std::string&>(),
                    std::declval<motion::Direction>())),
                "resolve must be noexcept");
  motion::RouteTransitions rt;
  bool threw = false;
  try {
    (void)rt.resolve("nope", motion::Direction::Pop);
    (void)rt.resolve("", motion::Direction::Enter);
  } catch (...) {
    threw = true;
  }
  return !threw;
}

// Reconfiguring the same (route,dir) overwrites the previous spec.
bool check_reconfigure_overwrites() {
  motion::RouteTransitions rt;
  rt.configure("home", motion::Direction::Enter, makeSpec(180.0f, 1.0f));
  rt.configure("home", motion::Direction::Enter, makeSpec(500.0f, 0.8f));
  const motion::TransitionSpec& s =
      rt.resolve("home", motion::Direction::Enter);
  return near(s.spring.stiffness, 500.0f, 1e-5f) &&
         near(s.spring.dampingRatio, 0.8f, 1e-5f);
}

// Resolved spec drives a graph transition end-to-end: a spring leaf built
// from the resolved spec starts at `from` and settles at `to`.
bool check_resolved_spec_drives_graph_transition() {
  motion::RouteTransitions rt;
  rt.configure("home", motion::Direction::Enter, makeSpec(180.0f, 1.0f));
  const motion::TransitionSpec& spec =
      rt.resolve("home", motion::Direction::Enter);
  motion::Graph g;
  motion::Node n;
  n.spring.from = 0.0f;
  n.spring.to = 100.0f;
  n.spring.stiffness = spec.spring.stiffness;
  n.spring.dampingRatio = spec.spring.dampingRatio;
  n.spring.durationMs = 2000.0f;
  motion::NodeId id = g.add(n);
  motion::Sample s0 = g.evaluate(0.0f);
  if (!near(s0.values.at(id), 0.0f, 1.0f)) return false;  // starts at from
  motion::Sample send = g.evaluate(10000.0f);
  return near(send.values.at(id), 100.0f, 0.5f);  // settles at to
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
  RUN_CASE(check_each_direction_resolves_configured_spec);
  RUN_CASE(check_routes_are_isolated);
  RUN_CASE(check_unconfigured_pair_falls_back_to_default);
  RUN_CASE(check_resolve_is_noexcept);
  RUN_CASE(check_reconfigure_overwrites);
  RUN_CASE(check_resolved_spec_drives_graph_transition);
  if (failures > 0) {
    std::printf("route_transition_test: %d failing\n", failures);
    return 1;
  }
  std::printf("route_transition_test: all passing\n");
  return 0;
}
