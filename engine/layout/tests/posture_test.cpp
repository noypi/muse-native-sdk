// T1 contract tests — FR-LAYOUT-002 / AC-LAYOUT-005 (foldable posture is
// derived from the real Android APIs — WindowManager / androidx.window.layout
// FoldingFeature plus the hinge-angle sensor — and drives layout without
// application-level bookkeeping).
// Maps to TC-LAYOUT-005 (`engine/layout/tests/posture_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services, no JS calls.
// Binds ONLY to the T1 `Interface:` line (reproduced verbatim in
// unit-tests/T1.md). Input shape mirrors androidx.window.layout
// WindowInfoTracker/FoldingFeature (state, orientation,
// occlusionType→OcclusionType, bounds) plus the hinge-angle sensor where the
// device exposes it (Q-LAYOUT-003 degrade rule).
//
// ASSUMPTION (enumerators): the `Interface:` line names the enum types but
// not their enumerators. These cases use PascalCase — FoldState::Flat,
// FoldState::HalfOpened, FoldState::Unknown; FoldOrientation::Vertical,
// FoldOrientation::Horizontal; OcclusionType::None, OcclusionType::Full —
// matching the existing `Posture` convention in layout/form_factor.h and the
// spec's FoldingFeature.State (FLAT, HALF_OPENED) / OcclusionType (NONE,
// FULL) semantics (specs-layout-styling.md, FR-LAYOUT-014 Notes). If the
// coder uses SCREAMING names, update call sites, not expectations.
// ASSUMPTION (orientation): orientation is opaque (as in the existing
// header); cases round-trip Vertical/Horizontal without asserting platform
// semantics.
// ASSUMPTION (Rect): `layout::Rect` is an aggregate of four floats read as
// x/y/w/h; cases assign/compare those field names.
// ASSUMPTION (posture visibility): the derived posture is observable via
// `current()` (per the Interface line "queryable via `current()`"); cases
// assert `current().state` and `current()` fields, never a separate posture
// accessor.
// ASSUMPTION (angle mapping): hingeAngleDeg round-trips through `current()`;
// no case asserts that a particular angle implies a particular posture (the
// Interface line pins no angle→state mapping).
// ASSUMPTION (no-JS proof): as in form_factor_test.cpp, the counter below
// can only be incremented by a cross-boundary call; no such call exists on
// this path, so the fold cases assert it stays 0.
#include <algorithm>
#include <cstdio>
#include <utility>
#include <vector>

#include "layout/form_factor.h"
#include "layout/solver.h"

namespace {

// Cross-boundary (JS) observer counter. Nothing on the posture path takes
// such a callback, so this must stay 0 through every fold event.
int g_jsObserverCalls = 0;

// Epsilon for float comparisons; fixture values are exactly
// representable in float.
constexpr float kEps = 1e-4f;

bool near(float a, float b) {
  return (a > b ? a - b : b - a) <= kEps;
}

bool rectNear(const layout::Rect& a, const layout::Rect& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) &&
         near(a.h, b.h);
}

bool statesEqual(const layout::FoldingFeatureState& a,
                 const layout::FoldingFeatureState& b) {
  return a.state == b.state && a.orientation == b.orientation &&
         a.occlusion == b.occlusion && a.hasHingeAngle == b.hasHingeAngle &&
         (!a.hasHingeAngle || near(a.hingeAngleDeg, b.hingeAngleDeg)) &&
         rectNear(a.bounds, b.bounds);
}

layout::FoldingFeatureState flat_no_sensor() {
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::Flat;
  s.orientation = layout::FoldOrientation::Vertical;
  s.occlusion = layout::OcclusionType::None;
  s.hingeAngleDeg = 0.0f;
  s.hasHingeAngle = false;
  s.bounds = layout::Rect{0.0f, 0.0f, 0.0f, 0.0f};
  return s;
}

layout::FoldingFeatureState half_opened_with_angle() {
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::HalfOpened;
  s.orientation = layout::FoldOrientation::Horizontal;
  s.occlusion = layout::OcclusionType::Full;
  s.hingeAngleDeg = 90.0f;
  s.hasHingeAngle = true;
  s.bounds = layout::Rect{0.0f, 390.0f, 720.0f, 20.0f};
  return s;
}

}  // namespace

// Compile-time: the folding-feature update path never throws (the Interface
// line pins `noexcept`; there is no JS/bridge hook to observe, so the
// runtime half is the g_jsObserverCalls == 0 assertion in the fold cases).
static_assert(noexcept(std::declval<layout::FormFactorSource&>()
                           .onFoldingFeatureChanged(
                               std::declval<layout::FoldingFeatureState>())),
              "onFoldingFeatureChanged must be noexcept");
static_assert(noexcept(std::declval<const layout::FormFactorSource&>().current()),
              "current must be noexcept");
// T5 (TC-LAYOUT-043 hermetic half): consumeForNextPass is noexcept and
// returns false when state is unchanged (extended semantics).
static_assert(noexcept(std::declval<layout::FormFactorSource&>()
                           .consumeForNextPass(
                               std::declval<layout::Solver&>(),
                               std::declval<layout::NodeId>())),
              "consumeForNextPass must be noexcept");

// Named case: a folding-feature update is queryable via current() field for
// field (state, orientation, occlusion, hinge angle + presence flag,
// bounds).
static bool check_update_current_reflects_all_fields() {
  layout::FormFactorSource src;
  const layout::FoldingFeatureState s = half_opened_with_angle();
  src.onFoldingFeatureChanged(s);
  const layout::FoldingFeatureState& cur = src.current();
  return cur.state == layout::FoldState::HalfOpened &&
         cur.orientation == layout::FoldOrientation::Horizontal &&
         cur.occlusion == layout::OcclusionType::Full &&
         cur.hasHingeAngle && near(cur.hingeAngleDeg, 90.0f) &&
         rectNear(cur.bounds, layout::Rect{0.0f, 390.0f, 720.0f, 20.0f});
}

// Named case (TC-LAYOUT-005 core): a scripted WindowManager/FoldingFeature +
// hinge-angle sequence yields the correct posture with no app-level
// bookkeeping — each callback fully determines current(), and no JS observer
// fires.
static bool check_scripted_sequence_yields_correct_posture() {
  layout::FormFactorSource src;
  // Frame 1: closed — flat, angle ~0, no occlusion, empty bounds.
  layout::FoldingFeatureState closed = flat_no_sensor();
  closed.hasHingeAngle = true;
  closed.hingeAngleDeg = 5.0f;
  src.onFoldingFeatureChanged(closed);
  if (src.current().state != layout::FoldState::Flat) return false;
  // Frame 2: opening — half-opened at ~90° with a FULL-occlusion hinge band.
  src.onFoldingFeatureChanged(half_opened_with_angle());
  const layout::FoldingFeatureState& cur = src.current();
  if (cur.state != layout::FoldState::HalfOpened) return false;
  if (cur.occlusion != layout::OcclusionType::Full) return false;
  if (!cur.hasHingeAngle || !near(cur.hingeAngleDeg, 90.0f)) return false;
  if (!rectNear(cur.bounds, layout::Rect{0.0f, 390.0f, 720.0f, 20.0f})) {
    return false;
  }
  // Frame 3: fully open — flat at 180°, occlusion cleared.
  layout::FoldingFeatureState open = closed;
  open.hingeAngleDeg = 180.0f;
  open.hasHingeAngle = true;
  src.onFoldingFeatureChanged(open);
  if (src.current().state != layout::FoldState::Flat) return false;
  if (!near(src.current().hingeAngleDeg, 180.0f)) return false;
  return g_jsObserverCalls == 0;
}

// Named case (Q-LAYOUT-003 degrade rule): hinge-angle sensor absent
// (hasHingeAngle == false) still resolves via bounds + occlusion — the state
// is queryable and carries the FULL-occlusion hinge rect.
static bool check_sensor_absent_resolves_via_bounds_and_occlusion() {
  layout::FormFactorSource src;
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::HalfOpened;
  s.orientation = layout::FoldOrientation::Vertical;
  s.occlusion = layout::OcclusionType::Full;
  s.hasHingeAngle = false;
  s.hingeAngleDeg = 0.0f;
  s.bounds = layout::Rect{350.0f, 0.0f, 20.0f, 800.0f};
  src.onFoldingFeatureChanged(s);
  const layout::FoldingFeatureState& cur = src.current();
  return cur.state == layout::FoldState::HalfOpened &&
         cur.occlusion == layout::OcclusionType::Full &&
         !cur.hasHingeAngle && near(cur.bounds.x, 350.0f) &&
         near(cur.bounds.w, 20.0f) && near(cur.bounds.h, 800.0f);
}

// Named case: unknown/absent sensor state degrades gracefully — no throw,
// still queryable via current().
static bool check_unknown_state_degrades_without_throwing() {
  layout::FormFactorSource src;
  bool threw = false;
  try {
    layout::FoldingFeatureState s = flat_no_sensor();
    s.state = layout::FoldState::Unknown;
    src.onFoldingFeatureChanged(s);
    (void)src.current();
  } catch (...) {
    threw = true;
  }
  return !threw && src.current().state == layout::FoldState::Unknown &&
         g_jsObserverCalls == 0;
}

// Named case (Interface error shape): repeating the identical state is a
// no-op — current() is field-for-field unchanged.
static bool check_unchanged_state_is_noop() {
  layout::FormFactorSource src;
  const layout::FoldingFeatureState s = half_opened_with_angle();
  src.onFoldingFeatureChanged(s);
  const layout::FoldingFeatureState first = src.current();
  src.onFoldingFeatureChanged(s);  // identical repeat.
  const layout::FoldingFeatureState second = src.current();
  return statesEqual(first, second);
}

// Named case: occlusion variants round-trip (NONE ↔ FULL).
static bool check_occlusion_variants_roundtrip() {
  layout::FormFactorSource src;
  layout::FoldingFeatureState s = flat_no_sensor();
  s.occlusion = layout::OcclusionType::Full;
  src.onFoldingFeatureChanged(s);
  if (src.current().occlusion != layout::OcclusionType::Full) return false;
  s.occlusion = layout::OcclusionType::None;
  src.onFoldingFeatureChanged(s);
  return src.current().occlusion == layout::OcclusionType::None;
}

// Named case: orientation round-trips opaquely (no platform semantics
// asserted).
static bool check_orientation_roundtrips_opaquely() {
  layout::FormFactorSource src;
  layout::FoldingFeatureState s = flat_no_sensor();
  s.orientation = layout::FoldOrientation::Horizontal;
  src.onFoldingFeatureChanged(s);
  if (src.current().orientation != layout::FoldOrientation::Horizontal) {
    return false;
  }
  s.orientation = layout::FoldOrientation::Vertical;
  src.onFoldingFeatureChanged(s);
  return src.current().orientation == layout::FoldOrientation::Vertical;
}

// Named case: extreme/malformed sensor values never throw and stay
// queryable (never-throws half of the contract under hostile input).
static bool check_extreme_values_never_throw_and_stay_queryable() {
  layout::FormFactorSource src;
  bool threw = false;
  try {
    layout::FoldingFeatureState s = flat_no_sensor();
    s.hingeAngleDeg = -45.0f;  // out-of-range sensor reading.
    s.hasHingeAngle = true;
    s.bounds = layout::Rect{-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f};
    src.onFoldingFeatureChanged(s);
    (void)src.current();
    layout::FoldingFeatureState empty{};
    empty.state = layout::FoldState::Unknown;
    empty.hasHingeAngle = false;
    src.onFoldingFeatureChanged(empty);
    (void)src.current();
  } catch (...) {
    threw = true;
  }
  return !threw &&
         src.current().state == layout::FoldState::Unknown &&
         g_jsObserverCalls == 0;
}

// ---- T5 unfold-scope cases (TC-LAYOUT-043 hermetic half) ----
// On HALF_OPENED↔FLAT transitions only subtrees intersecting the hinge
// region (the FoldingFeature bounds rect under FULL occlusion) are
// re-solved; the pass performs no Activity recreation (in-place contract:
// no throw, no JS/cross-boundary call) and leaves no content under a FULL
// hinge. Device-lab half of TC-LAYOUT-043 stays external (see T5.md).
//
// ASSUMPTION (hinge source): the hinge region is the FoldingFeature
// `bounds` rect carried by `onFoldingFeatureChanged` when
// `occlusion == OcclusionType::Full`. Cases drive the transition purely via
// `onFoldingFeatureChanged` + `consumeForNextPass`/`affectedSubtrees` and
// assert solved-box geometry; they never touch the compat
// `onWindowLayoutChanged` path.
// ASSUMPTION (unaffected stability): "re-solves only hinge-affected
// subtrees" is observable as byte-identical (epsilon) solved boxes for
// non-intersecting siblings across the pass; no per-node dirty-set accessor
// is asserted (the Interface line pins none).
// ASSUMPTION (no-content proof): a solved box leaves "no content under FULL
// hinge" when it does not overlap the hinge rect with nonzero area;
// fixtures keep the hinge strictly inside/outside child boxes, so
// edge-touch semantics stay unasserted.

namespace t5 {

struct ThreeCol {
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId cols[3];
};

ThreeCol build_three_col(layout::Solver& solver) {
  ThreeCol t;
  t.root = solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(t.root, 720.0f, 800.0f, 0.0f);
  const float widths[3] = {200.0f, 200.0f, 320.0f};
  for (int i = 0; i < 3; ++i) {
    t.cols[i] = solver.addNode(layout::Backend::Flex, t.root);
    solver.setFlex(t.cols[i], widths[i], 800.0f, 0.0f);
  }
  return t;
}

layout::FoldingFeatureState flat_state() {
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::Flat;
  s.orientation = layout::FoldOrientation::Vertical;
  s.occlusion = layout::OcclusionType::None;
  s.hingeAngleDeg = 180.0f;
  s.hasHingeAngle = true;
  s.bounds = layout::Rect{0.0f, 0.0f, 0.0f, 0.0f};
  return s;
}

// FULL-occlusion hinge strictly inside the middle column (200..400):
// x 290..330, full height.
layout::FoldingFeatureState half_opened_middle_hinge() {
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::HalfOpened;
  s.orientation = layout::FoldOrientation::Vertical;
  s.occlusion = layout::OcclusionType::Full;
  s.hingeAngleDeg = 90.0f;
  s.hasHingeAngle = true;
  s.bounds = layout::Rect{290.0f, 0.0f, 40.0f, 800.0f};
  return s;
}

bool boxNear(const layout::Box& a, const layout::Box& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) &&
         near(a.h, b.h);
}

bool boxOverlapsRect(const layout::Box& b, const layout::Rect& r) {
  return b.x < r.x + r.w && r.x < b.x + b.w && b.y < r.y + r.h &&
         r.y < b.y + b.h;
}

bool sameSet(std::vector<layout::NodeId> got,
             std::vector<layout::NodeId> want) {
  std::sort(got.begin(), got.end());
  std::sort(want.begin(), want.end());
  return got == want;
}

}  // namespace t5

// Named case (T5 error shape): repeating the identical folding state is a
// no-op — the second consumeForNextPass returns false (no re-solve).
static bool check_unfold_consume_false_when_unchanged() {
  layout::Solver solver;
  t5::ThreeCol t = t5::build_three_col(solver);
  solver.solve(t.root);
  layout::FormFactorSource src;
  src.onFoldingFeatureChanged(t5::half_opened_middle_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;
  src.onFoldingFeatureChanged(t5::half_opened_middle_hinge());
  return !src.consumeForNextPass(solver, t.root);
}

// Named case (TC-LAYOUT-043 hermetic core): HALF_OPENED→FLAT→HALF_OPENED
// re-solves only hinge-affected subtrees — the affected set is exactly the
// intersecting middle column, and unaffected siblings keep byte-identical
// boxes across the fold pass with no JS/recreation call.
static bool check_unfold_resolves_only_hinge_affected_subtrees() {
  layout::Solver solver;
  t5::ThreeCol t = t5::build_three_col(solver);
  solver.solve(t.root);
  const layout::Box before0 = solver.boxOf(t.cols[0]);
  const layout::Box before2 = solver.boxOf(t.cols[2]);
  layout::FormFactorSource src;
  src.onFoldingFeatureChanged(t5::flat_state());
  src.consumeForNextPass(solver, t.root);
  src.onFoldingFeatureChanged(t5::half_opened_middle_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;
  if (!t5::sameSet(src.affectedSubtrees(solver, t.root), {t.cols[1]})) {
    return false;
  }
  const layout::Box& after0 = solver.boxOf(t.cols[0]);
  const layout::Box& after2 = solver.boxOf(t.cols[2]);
  return t5::boxNear(after0, before0) && t5::boxNear(after2, before2) &&
         g_jsObserverCalls == 0;
}

// Named case: FULL-occlusion hinge leaves no content under the hinge rect
// after the fold pass — no solved child box overlaps the hinge region.
static bool check_unfold_full_hinge_leaves_no_content_under_hinge() {
  layout::Solver solver;
  t5::ThreeCol t = t5::build_three_col(solver);
  solver.solve(t.root);
  layout::FormFactorSource src;
  src.onFoldingFeatureChanged(t5::flat_state());
  src.consumeForNextPass(solver, t.root);
  src.onFoldingFeatureChanged(t5::half_opened_middle_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;
  const layout::Rect hinge = t5::half_opened_middle_hinge().bounds;
  for (int i = 0; i < 3; ++i) {
    if (t5::boxOverlapsRect(solver.boxOf(t.cols[i]), hinge)) return false;
  }
  return true;
}

// Named case: unfold round-trip HALF_OPENED→FLAT restores every child frame
// to its pre-fold value (no stale hinge shift survives hinge removal) with
// no recreation call.
static bool check_unfold_roundtrip_restores_frames_no_recreation() {
  layout::Solver solver;
  t5::ThreeCol t = t5::build_three_col(solver);
  solver.solve(t.root);
  const layout::Box pre0 = solver.boxOf(t.cols[0]);
  const layout::Box pre1 = solver.boxOf(t.cols[1]);
  const layout::Box pre2 = solver.boxOf(t.cols[2]);
  layout::FormFactorSource src;
  src.onFoldingFeatureChanged(t5::flat_state());
  src.consumeForNextPass(solver, t.root);
  src.onFoldingFeatureChanged(t5::half_opened_middle_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;
  src.onFoldingFeatureChanged(t5::flat_state());
  src.consumeForNextPass(solver, t.root);
  const layout::Box& post0 = solver.boxOf(t.cols[0]);
  const layout::Box& post1 = solver.boxOf(t.cols[1]);
  const layout::Box& post2 = solver.boxOf(t.cols[2]);
  return t5::boxNear(post0, pre0) && t5::boxNear(post1, pre1) &&
         t5::boxNear(post2, pre2) && g_jsObserverCalls == 0;
}

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
  RUN_CASE(check_update_current_reflects_all_fields);
  RUN_CASE(check_scripted_sequence_yields_correct_posture);
  RUN_CASE(check_sensor_absent_resolves_via_bounds_and_occlusion);
  RUN_CASE(check_unknown_state_degrades_without_throwing);
  RUN_CASE(check_unchanged_state_is_noop);
  RUN_CASE(check_occlusion_variants_roundtrip);
  RUN_CASE(check_orientation_roundtrips_opaquely);
  RUN_CASE(check_extreme_values_never_throw_and_stay_queryable);
  RUN_CASE(check_unfold_consume_false_when_unchanged);
  RUN_CASE(check_unfold_resolves_only_hinge_affected_subtrees);
  RUN_CASE(check_unfold_full_hinge_leaves_no_content_under_hinge);
  RUN_CASE(check_unfold_roundtrip_restores_frames_no_recreation);
  if (failures > 0) {
    std::printf("posture_test: %d failing\n", failures);
    return 1;
  }
  std::printf("posture_test: all passing\n");
  return 0;
}
