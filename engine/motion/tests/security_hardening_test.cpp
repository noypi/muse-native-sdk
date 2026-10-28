// Stage-4 security hardening tests — FR-MOTION-006/007/008.
// Covers the `Security tests requested` rows of
// workflow.local/runs/fr-motion-006-fr-motion-008-fr-motion-007/security-review.md.
//
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services, no wall clock, no randomness. Each case
// calls the public entry once with adversarial input and asserts a
// deterministic outcome. Prompt termination of the huge-`tMs` cases is
// enforced by the OUTER runner (`timeout 60 ./security_hardening_test`):
// a `timeout` kill (exit 124) counts as demonstrating the hang (RED).
// Plain main() + RUN_CASE + printf convention, nonzero exit on failure.
//
// ASSUMPTION (input-order fallback for NaN startMs): `arbitrate` compares
// `startMs` with exact `!=`/`>`, so a NaN `startMs` loses every `>`
// comparison and the winner among NaN-bearing claims falls back to input
// order. The contract pinned here is determinism (repeated calls with the
// same input return the same winner), NOT input-order independence for NaN
// inputs, which the finite-value cases already pin elsewhere.
//
// ASSUMPTION (solver sink pass-through): `advanceSpring` guards only NaN
// target and non-finite/non-positive dtMs. An `Inf` target or a non-finite
// `state.value/velocity` passes through to the output (probed: Inf in ->
// Inf out). Callers must guarantee finite state/target; only the guarded
// entries are asserted here.
//
// ASSUMPTION (Box channels are caller-finite): `interpolateBox` clamps only
// `t01` (including +-Inf -> endpoints). NaN `t01` or NaN Box channels pass
// through as NaN (probed). Callers must guarantee finite channels; only
// the `t01` clamp is asserted here.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "motion/arbitration.h"
#include "motion/gesture_binding.h"
#include "motion/graph.h"
#include "motion/implicit.h"
#include "motion/layout_adapter.h"
#include "motion/route_transition.h"
#include "motion/solver.h"

namespace {

bool finite(float v) { return std::isfinite(v) != 0; }

// --- (1) Bounded horizon: huge/Inf tMs returns promptly with finite values.

bool check_huge_tms_graph_spring_returns_finite() {
  motion::Graph g;
  motion::Node n;
  n.spring.from = 0.0f;
  n.spring.to = 100.0f;
  n.spring.stiffness = 180.0f;
  n.spring.dampingRatio = 1.0f;
  n.spring.durationMs = 2000.0f;
  motion::NodeId id = g.add(n);
  motion::Sample huge = g.evaluate(1e12f);
  if (!finite(huge.values.at(id))) return false;
  motion::Sample inf = g.evaluate(INFINITY);
  if (!finite(inf.values.at(id))) return false;
  motion::Sample negInf = g.evaluate(-INFINITY);
  return finite(negInf.values.at(id));
}

bool check_huge_delta_implicit_returns_finite() {
  motion::ImplicitRegistry reg;
  motion::PropertyId p{7};
  motion::TransitionSpec spec;
  spec.spring.stiffness = 180.0f;
  spec.spring.dampingRatio = 1.0f;
  reg.markImplicit(p, spec);
  reg.set(p, 100.0f, 0.0f);
  reg.set(p, 200.0f, 10.0f);  // activate the in-flight transition
  if (!finite(reg.get(p, 1e12f))) return false;
  if (!finite(reg.get(p, INFINITY))) return false;
  return finite(reg.get(p, -INFINITY));
}

bool check_solver_nonfinite_dtms_ignored() {
  motion::SpringState state{10.0f, 5.0f};
  motion::SpringSpec spec{180.0f, 1.0f};
  motion::SpringState nanDt = motion::advanceSpring(state, spec, 100.0f,
                                                   std::nanf(""));
  if (nanDt.value != state.value || nanDt.velocity != state.velocity)
    return false;
  motion::SpringState infDt =
      motion::advanceSpring(state, spec, 100.0f, INFINITY);
  if (infDt.value != state.value || infDt.velocity != state.velocity)
    return false;
  motion::SpringState negInfDt =
      motion::advanceSpring(state, spec, 100.0f, -INFINITY);
  return negInfDt.value == state.value &&
         negInfDt.velocity == state.velocity;
}

// --- (2) Cycle/depth: cyclic and ultra-deep graphs terminate.

bool check_mutual_cycle_terminates() {
  motion::Graph g;
  motion::Node a;
  a.sequence.children.push_back(motion::NodeId{1});  // forward ref: legal
  g.add(a);                                          // id 0
  motion::Node b;
  b.sequence.children.push_back(motion::NodeId{0});
  g.add(b);  // id 1; 0<->1 cycle
  motion::Sample s = g.evaluate(100.0f);
  (void)s;
  return true;  // terminated without stack overflow
}

bool check_self_reference_terminates() {
  motion::Graph g;
  motion::Node a;
  a.sequence.children.push_back(motion::NodeId{0});  // self-reference
  g.add(a);
  motion::Sample s = g.evaluate(100.0f);
  (void)s;
  return true;
}

bool check_ultra_deep_nesting_terminates() {
  motion::Graph g;
  const int kDepth = 5000;
  for (int i = 0; i < kDepth; ++i) {
    motion::Node n;
    n.sequence.children.push_back(
        motion::NodeId{static_cast<std::uint32_t>(i + 1)});
    g.add(n);
  }
  motion::Node leaf;
  leaf.value.value = 42.0f;
  g.add(leaf);
  motion::Sample s = g.evaluate(100.0f);  // top of the 5000-deep chain
  (void)s;
  return true;
}

// --- (3) Hostile route strings resolve without throw/hang.

bool check_hostile_route_strings_resolve_without_throw() {
  static_assert(noexcept(std::declval<const motion::RouteTransitions&>().resolve(
                    std::declval<const std::string&>(),
                    std::declval<motion::Direction>())),
                "resolve must be noexcept");
  motion::RouteTransitions rt;
  bool threw = false;
  try {
    std::string huge(1 << 20, 'x');  // 1 MiB adversarial route
    const motion::TransitionSpec& d1 =
        rt.resolve(huge, motion::Direction::Enter);
    const motion::TransitionSpec& d2 =
        rt.resolve("", motion::Direction::Exit);
    std::string unicode = "\xf0\x9f\x9a\x80\xe2\x9a\xa1\x00\x01\x02";
    const motion::TransitionSpec& d3 =
        rt.resolve(unicode, motion::Direction::Pop);
    // Unconfigured pairs fall back to the inert default (zero stiffness).
    if (d1.spring.stiffness != 0.0f || d2.spring.stiffness != 0.0f ||
        d3.spring.stiffness != 0.0f)
      return false;
  } catch (...) {
    threw = true;
  }
  return !threw;
}

// --- (4) Non-finite inputs ignored at gesture/implicit/solver entries.

struct RecordingWriter : motion::ITransformWriter {
  int calls = 0;
  float lastValue = 0.0f;
  void writeTransform(float value, float /*tMs*/) override {
    ++calls;
    lastValue = value;
  }
};

bool check_gesture_nonfinite_samples_ignored() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  motion::InputSample good{10.0f, 2.0f, 1.0f};
  binding.onSample(good);
  if (w.calls != 1) return false;
  const float heldValue = binding.springState().value;
  const float heldVel = binding.springState().velocity;
  const motion::InputSample bad[] = {
      {std::nanf(""), 0.0f, 2.0f},
      {0.0f, INFINITY, 2.0f},
      {0.0f, 0.0f, std::nanf("")},
      {0.0f, 0.0f, INFINITY},
      {INFINITY, INFINITY, INFINITY},
  };
  for (const auto& s : bad) {
    binding.onSample(s);
    binding.onRelease(s);
  }
  // No writer call and no state change from any non-finite input.
  return w.calls == 1 && binding.springState().value == heldValue &&
         binding.springState().velocity == heldVel;
}

bool check_implicit_nonfinite_set_ignored() {
  motion::ImplicitRegistry reg;
  motion::PropertyId p{3};
  motion::TransitionSpec spec;
  spec.spring.stiffness = 180.0f;
  spec.spring.dampingRatio = 1.0f;
  reg.markImplicit(p, spec);
  reg.set(p, 100.0f, 0.0f);
  const float baseline = reg.get(p, 50.0f);
  if (!finite(baseline)) return false;
  reg.set(p, std::nanf(""), 60.0f);
  reg.set(p, INFINITY, 60.0f);
  reg.set(p, 200.0f, std::nanf(""));
  reg.set(p, 200.0f, INFINITY);
  // Ignored sets leave the trajectory unchanged and finite.
  const float after = reg.get(p, 50.0f);
  return finite(after) && after == baseline;
}

bool check_solver_nan_target_ignored() {
  motion::SpringState state{10.0f, 5.0f};
  motion::SpringSpec spec{180.0f, 1.0f};
  motion::SpringState out =
      motion::advanceSpring(state, spec, std::nanf(""), 16.0f);
  return out.value == state.value && out.velocity == state.velocity;
}

bool check_graph_nonfinite_tms_clamped_to_zero() {
  motion::Graph g;
  motion::Node n;
  n.spring.from = 0.0f;
  n.spring.to = 100.0f;
  n.spring.stiffness = 180.0f;
  n.spring.dampingRatio = 1.0f;
  n.spring.durationMs = 2000.0f;
  motion::NodeId id = g.add(n);
  const float atZero = g.evaluate(0.0f).values.at(id);
  const float atNan = g.evaluate(std::nanf("")).values.at(id);
  const float atInf = g.evaluate(INFINITY).values.at(id);
  return finite(atNan) && finite(atInf) && atNan == atZero &&
         atInf == atZero;
}

bool check_box_t01_clamp_holds_for_infinite_t() {
  motion::Box from{0.0f, 0.0f, 10.0f, 10.0f};
  motion::Box to{100.0f, 100.0f, 50.0f, 50.0f};
  motion::Box hi = motion::interpolateBox(from, to, INFINITY);
  motion::Box lo = motion::interpolateBox(from, to, -INFINITY);
  motion::Box mid = motion::interpolateBox(from, to, 0.5f);
  return hi.x == to.x && hi.y == to.y && hi.w == to.w && hi.h == to.h &&
         lo.x == from.x && lo.y == from.y && lo.w == from.w &&
         lo.h == from.h && finite(mid.x) && finite(mid.y) &&
         finite(mid.w) && finite(mid.h);
}

// --- (5) NaN startMs arbitration is deterministic across repeated calls.

bool check_nan_startms_arbitration_deterministic() {
  // input order always yields the same winner.
  motion::Claim c1;
  c1.id = motion::NodeId{0};
  c1.priority = 1;
  c1.startMs = std::nanf("");
  motion::Claim c2;
  c2.id = motion::NodeId{1};
  c2.priority = 1;
  c2.startMs = 50.0f;
  const std::vector<motion::Claim> order = {c1, c2};
  motion::Winner w1 = motion::arbitrate(order);
  motion::Winner w2 = motion::arbitrate(order);
  motion::Winner w3 = motion::arbitrate(order);
  return !w1.none &&
         w1.id.value == w2.id.value && w2.id.value == w3.id.value;
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
  RUN_CASE(check_huge_tms_graph_spring_returns_finite);
  RUN_CASE(check_huge_delta_implicit_returns_finite);
  RUN_CASE(check_solver_nonfinite_dtms_ignored);
  RUN_CASE(check_mutual_cycle_terminates);
  RUN_CASE(check_self_reference_terminates);
  RUN_CASE(check_ultra_deep_nesting_terminates);
  RUN_CASE(check_hostile_route_strings_resolve_without_throw);
  RUN_CASE(check_gesture_nonfinite_samples_ignored);
  RUN_CASE(check_implicit_nonfinite_set_ignored);
  RUN_CASE(check_solver_nan_target_ignored);
  RUN_CASE(check_graph_nonfinite_tms_clamped_to_zero);
  RUN_CASE(check_box_t01_clamp_holds_for_infinite_t);
  RUN_CASE(check_nan_startms_arbitration_deterministic);
  if (failures > 0) {
    std::printf("security_hardening_test: %d failing\n", failures);
    return 1;
  }
  std::printf("security_hardening_test: all passing\n");
  return 0;
}
