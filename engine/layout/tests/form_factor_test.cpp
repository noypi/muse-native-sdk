// T6 contract tests — FR-LAYOUT-006 / AC-LAYOUT-012 (form-factor from real
// Android window APIs applied to the next pass) + AC-LAYOUT-013 fold half,
// FR-LAYOUT-014 AC-040/041/043 as consumption seam only (this run binds the
// posture signal shape; signal emission stays in FR-014).
// Maps to TC-LAYOUT-012 (`engine/layout/tests/form_factor_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services, no JS calls.
// Binds ONLY to the T6 `Interface:` line (reproduced verbatim in
// unit-tests/T6.md). Input shape mirrors
// androidx.window.layout WindowInfoTracker/FoldingFeature (state,
// orientation, occlusionType→hingeOccludes, bounds).
//
// ASSUMPTION (subtree granularity): the `Interface:` line says
// `affectedSubtrees` "returns subtree roots intersecting the hinge rect"
// without fixing the granularity. These cases build a root whose direct
// children are the candidate subtrees and assert the returned set contains
// exactly the intersecting direct children (order-insensitive). If the coder
// returns deeper delegate-registered roots instead, the intersecting-set
// property pins the behavior — update call sites, not expectations.
//
// ASSUMPTION (orientation): `orientation` is an opaque int carried from
// `FoldingFeature#getOrientation`; tests round-trip it without asserting
// platform semantics.
//
// ASSUMPTION (intersection): a subtree intersects when its solved box
// overlaps the hinge rect with nonzero area. Fixtures keep the hinge
// strictly inside (or strictly outside) child boxes, so edge-touch
// semantics are never asserted.
//
// ASSUMPTION (query precondition): tests call `solver.solve(root)` before
// `affectedSubtrees`, so boxes are available via `boxOf`. Behavior on an
// unsolved solver is not asserted (the `Interface:` line pins nothing).
//
// ASSUMPTION (no-JS proof): the `Interface:` line exposes no JS/bridge
// callback, so there is nothing to observe. The counter below exists to make
// that airtight: it can only be incremented by a cross-boundary call, no
// such call exists on this path, and the fold cases assert it stays 0.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "layout/form_factor.h"
#include "layout/solver.h"

namespace {

// Cross-boundary (JS) observer counter. Nothing on the form-factor path
// takes such a callback, so this must stay 0 through every fold event.
int g_jsObserverCalls = 0;

// Epsilon for float comparisons; fixture values are exactly
// representable in float.
constexpr float kEps = 1e-4f;

bool near(float a, float b) {
  return (a > b ? a - b : b - a) <= kEps;
}

// Vertical-hinge fixture: root 720x800, three fixed children in flex row
// (+x, per Solver::layoutFlexSubtree): c0 x 0..200, c1 x 200..400,
// c2 x 400..720.
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

layout::FormFactorState flat_no_hinge() {
  layout::FormFactorState s;
  s.posture = layout::Posture::Flat;
  s.hasHinge = false;
  s.hingeX = 0.0f;
  s.hingeY = 0.0f;
  s.hingeW = 0.0f;
  s.hingeH = 0.0f;
  s.hingeOccludes = false;
  s.orientation = 1;
  return s;
}

// FULL-occlusion vertical hinge strictly inside the middle column:
// x 290..330 (middle column spans 200..400), full height.
layout::FormFactorState half_opened_middle_hinge() {
  layout::FormFactorState s;
  s.posture = layout::Posture::HalfOpened;
  s.hasHinge = true;
  s.hingeX = 290.0f;
  s.hingeY = 0.0f;
  s.hingeW = 40.0f;
  s.hingeH = 800.0f;
  s.hingeOccludes = true;
  s.orientation = 1;
  return s;
}

bool same_set(std::vector<layout::NodeId> got,
              std::vector<layout::NodeId> want) {
  std::sort(got.begin(), got.end());
  std::sort(want.begin(), want.end());
  return got == want;
}

}  // namespace

// Compile-time: the update path never throws and never calls into JS
// (no JS hook exists in the interface; noexcept is the machine-checked half).
static_assert(noexcept(std::declval<layout::FormFactorSource&>()
                           .onWindowLayoutChanged(
                               std::declval<const layout::FormFactorState&>())),
              "onWindowLayoutChanged must be noexcept");
static_assert(noexcept(std::declval<const layout::FormFactorSource&>().current()),
              "current must be noexcept");

// Named case: state update → current() reflects it field for field.
static bool check_state_update_current_reflects() {
  layout::FormFactorSource src;
  const layout::FormFactorState s = half_opened_middle_hinge();
  src.onWindowLayoutChanged(s);
  const layout::FormFactorState& cur = src.current();
  return cur.posture == layout::Posture::HalfOpened && cur.hasHinge &&
         near(cur.hingeX, 290.0f) && near(cur.hingeY, 0.0f) &&
         near(cur.hingeW, 40.0f) && near(cur.hingeH, 800.0f) &&
         cur.hingeOccludes && cur.orientation == 1;
}

// Named case: unchanged state → consumeForNextPass returns false (no-op).
// The first consume after a change returns true (work to do); repeating the
// identical state is a no-op.
static bool check_unchanged_state_consume_false() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(flat_no_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;  // first: work
  src.onWindowLayoutChanged(flat_no_hinge());  // identical state
  return !src.consumeForNextPass(solver, t.root);  // no-op pass
}

// Named case: hinge rect → exactly the intersecting subtrees (middle only).
static bool check_hinge_rect_exactly_intersecting_subtrees() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(half_opened_middle_hinge());
  return same_set(src.affectedSubtrees(solver, t.root), {t.cols[1]});
}

// Named case: wide hinge spanning two columns returns exactly those two.
static bool check_wide_hinge_returns_both_intersected() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorState s = half_opened_middle_hinge();
  s.hingeX = 150.0f;  // 150..350 spans c0 (0..200) and c1 (200..400).
  s.hingeW = 200.0f;
  layout::FormFactorSource src;
  src.onWindowLayoutChanged(s);
  return same_set(src.affectedSubtrees(solver, t.root),
                  {t.cols[0], t.cols[1]});
}

// Named case: hinge-absent state degrades to full-width (no split) —
// affectedSubtrees is empty even with a stale rect, and consume still drives
// a full-width pass.
static bool check_hinge_absent_degrades_to_full_width() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorState s = half_opened_middle_hinge();
  s.hasHinge = false;
  s.hingeW = 0.0f;
  s.hingeH = 0.0f;
  layout::FormFactorSource src;
  src.onWindowLayoutChanged(s);
  if (!src.affectedSubtrees(solver, t.root).empty()) return false;
  // Full-width frames survive the pass: outer columns keep exact boxes.
  src.consumeForNextPass(solver, t.root);
  const layout::Box& c0 = solver.boxOf(t.cols[0]);
  const layout::Box& c2 = solver.boxOf(t.cols[2]);
  return near(c0.x, 0.0f) && near(c0.w, 200.0f) && near(c2.x, 400.0f) &&
         near(c2.w, 320.0f);
}

// Named case: hinge missing entirely (rect far from content) → empty set.
static bool check_hinge_outside_content_returns_empty() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorState s = half_opened_middle_hinge();
  s.hingeX = 1000.0f;  // outside the 720-wide root.
  s.hingeW = 40.0f;
  layout::FormFactorSource src;
  src.onWindowLayoutChanged(s);
  return src.affectedSubtrees(solver, t.root).empty();
}

// Named case: Posture::Unknown with no hinge never splits.
static bool check_unknown_posture_no_hinge_no_split() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorState s = flat_no_hinge();
  s.posture = layout::Posture::Unknown;
  layout::FormFactorSource src;
  src.onWindowLayoutChanged(s);
  return src.affectedSubtrees(solver, t.root).empty();
}

// Named case: flat→half-opened re-solves only hinge-affected subtrees —
// unaffected columns keep byte-identical boxes across consumeForNextPass,
// and no JS observer fires (counter stays 0).
static bool check_fold_resolves_only_affected_subtrees_no_js() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);
  const layout::Box before0 = solver.boxOf(t.cols[0]);
  const layout::Box before2 = solver.boxOf(t.cols[2]);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(flat_no_hinge());
  src.consumeForNextPass(solver, t.root);
  src.onWindowLayoutChanged(half_opened_middle_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;

  const layout::Box& after0 = solver.boxOf(t.cols[0]);
  const layout::Box& after2 = solver.boxOf(t.cols[2]);
  if (!(near(after0.x, before0.x) && near(after0.y, before0.y) &&
        near(after0.w, before0.w) && near(after0.h, before0.h)))
    return false;
  if (!(near(after2.x, before2.x) && near(after2.y, before2.y) &&
        near(after2.w, before2.w) && near(after2.h, before2.h)))
    return false;
  return g_jsObserverCalls == 0;
}

// Named case (Stage 3 regression, review BLOCKER): flat → half-opened FULL
// hinge → flat again on a long-lived Solver restores every child frame to
// its pre-fold value (stale hinge pins must not survive hinge removal).
static bool check_unfold_roundtrip_restores_frames() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);
  const layout::Box pre0 = solver.boxOf(t.cols[0]);
  const layout::Box pre1 = solver.boxOf(t.cols[1]);
  const layout::Box pre2 = solver.boxOf(t.cols[2]);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(flat_no_hinge());
  src.consumeForNextPass(solver, t.root);
  src.onWindowLayoutChanged(half_opened_middle_hinge());
  if (!src.consumeForNextPass(solver, t.root)) return false;
  src.onWindowLayoutChanged(flat_no_hinge());
  src.consumeForNextPass(solver, t.root);

  const layout::Box& post0 = solver.boxOf(t.cols[0]);
  const layout::Box& post1 = solver.boxOf(t.cols[1]);
  const layout::Box& post2 = solver.boxOf(t.cols[2]);
  auto same = [](const layout::Box& a, const layout::Box& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) &&
           near(a.h, b.h);
  };
  return same(post0, pre0) && same(post1, pre1) && same(post2, pre2);
}

// Named case (Stage 3 regression, review BLOCKER): repeated fold/unfold
// toggles must not grow the constraint count (hinge pins have a defined
// lifetime via the constraintCount snapshot/truncate rule).
static bool check_repeated_toggles_do_not_grow_constraints() {
  layout::Solver solver;
  ThreeCol t = build_three_col(solver);
  solver.solve(t.root);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(flat_no_hinge());
  src.consumeForNextPass(solver, t.root);
  const std::size_t base = solver.constraintCount();
  for (int i = 0; i < 12; ++i) {
    src.onWindowLayoutChanged(half_opened_middle_hinge());
    src.consumeForNextPass(solver, t.root);
    src.onWindowLayoutChanged(flat_no_hinge());
    src.consumeForNextPass(solver, t.root);
  }
  return solver.constraintCount() == base;
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
  RUN_CASE(check_state_update_current_reflects);
  RUN_CASE(check_unchanged_state_consume_false);
  RUN_CASE(check_hinge_rect_exactly_intersecting_subtrees);
  RUN_CASE(check_wide_hinge_returns_both_intersected);
  RUN_CASE(check_hinge_absent_degrades_to_full_width);
  RUN_CASE(check_hinge_outside_content_returns_empty);
  RUN_CASE(check_unknown_posture_no_hinge_no_split);
  RUN_CASE(check_fold_resolves_only_affected_subtrees_no_js);
  RUN_CASE(check_unfold_roundtrip_restores_frames);
  RUN_CASE(check_repeated_toggles_do_not_grow_constraints);
  if (failures > 0) {
    std::printf("form_factor_test: %d failing\n", failures);
    return 1;
  }
  std::printf("form_factor_test: all passing\n");
  return 0;
}
