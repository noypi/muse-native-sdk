// T4+T5 contract tests — FR-LAYOUT-006 / AC-LAYOUT-013, sliver half (T4) and
// masonry half (T5) of TC-LAYOUT-013. Fold + integration sections belong to
// later batches (T6, T10) and are NOT added here per the exact-test-file-
// manifest rule (this file is assigned to its first item T4; T5 maps to it).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T4/T5 `Interface:` lines (reproduced verbatim in
// unit-tests/T4.md and unit-tests/T5.md). Geometry assertions use exact float
// frames with an epsilon helper (kEps = 1e-4, documented below); every
// expected value in the fixtures is exactly representable in float, so the
// epsilon only guards intermediate arithmetic (e.g. collapse interpolation).
//
// ASSUMPTION (sliver child order): the T4 `Interface:` line names no node
// binding — the constructor takes only `SliverConfig`. These cases build a
// 3-child subtree under the delegate root in order
// [toolbar, content, pinned-header] and the delegate lays out its subtree
// children in that positional order (the only ordering expressible without
// extra API). If the coder settles a different binding (e.g. explicit node
// ids), the expected frames below pin the behavior, not the child order:
// update call sites, not expectations.
//
// ASSUMPTION (sliver content size): content width/height ride the flex sizes
// set before registration (360x600); the delegate owns the toolbar height
// (interpolated) and the pinned-header stick position. The root carries the
// viewport size (360x800).
//
// ASSUMPTION (sliver pinned rule): pinned header y = max(contentY,
// scrollOffset) with contentY = toolbarH (content directly under the
// toolbar), per the task statement. Toolbar height = expandedH +
// fraction * (collapsedH - expandedH), fraction = clamp(offset /
// (expandedH - collapsedH), 0, 1).
//
// ASSUMPTION (masonry item binding): the T5 `Interface:` line names no
// container/item binding either. These cases register the delegate at a
// container root whose children, in insertion order, are the masonry items;
// item heights come from setEstimatedHeight/setMeasuredHeight (estimates
// until measured). Column width = (containerW - (columns-1)*columnGap) /
// columns; item x = col * (columnW + columnGap); y stacks greedily on the
// shortest column with itemGap between items.
//
// ASSUMPTION (masonry correction scope): estimate→measure correction shifts
// only items at/below the changed item in the same column; other columns are
// untouched. Tests assert exactly that via boxOf before/after.
#include <cmath>
#include <cstddef>
#include <cstdio>

#include "layout/delegate.h"
#include "layout/masonry.h"
#include "layout/sliver.h"
#include "layout/solver.h"

namespace {

// Epsilon for geometry assertions: 1e-4 (<= 1e-4 per batch brief). All
// fixture values are exactly representable in float; the epsilon guards only
// intermediate float arithmetic such as the collapse interpolation.
constexpr float kEps = 1e-4f;

bool near(float a, float b) { return std::fabs(a - b) <= kEps; }

bool near_box(const layout::Box& got, float x, float y, float w, float h) {
  return near(got.x, x) && near(got.y, y) && near(got.w, w) && near(got.h, h);
}

// ---- Sliver fixture ----
// Viewport 360x800. Config: collapsedH=56, expandedH=200, pinnedH=48.
// Collapse range = 144. Children of the delegate root in order:
// toolbar (id t), content 360x600 (id c), pinned header (id p, height 48).
struct SliverTree {
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId toolbar = layout::kInvalidNodeId;
  layout::NodeId content = layout::kInvalidNodeId;
  layout::NodeId pinned = layout::kInvalidNodeId;
};

SliverTree build_sliver_tree(layout::Solver& solver) {
  SliverTree t;
  t.root = solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(t.root, 360.0f, 800.0f, 0.0f);
  t.toolbar = solver.addNode(layout::Backend::Flex, t.root);
  solver.setFlex(t.toolbar, 360.0f, 200.0f, 0.0f);
  t.content = solver.addNode(layout::Backend::Flex, t.root);
  solver.setFlex(t.content, 360.0f, 600.0f, 0.0f);
  t.pinned = solver.addNode(layout::Backend::Flex, t.root);
  solver.setFlex(t.pinned, 360.0f, 48.0f, 0.0f);
  return t;
}

layout::SliverConfig sliver_config() {
  layout::SliverConfig c;
  c.collapsedH = 56.0f;
  c.expandedH = 200.0f;
  c.pinnedH = 48.0f;
  return c;
}

// ---- Masonry fixture ----
// Container 360 wide, 2 columns, columnGap=8, itemGap=8.
// Column width = (360 - 8) / 2 = 176; column x origins: 0, 184.
// Six items, heights [100, 60, 80, 120, 40, 90] → greedy shortest-column:
//   col heights: [0,0] → i0→col0 [100] → i1→col1 [60] → i2→col1 [148] →
//   i3→col0 [228] → i4→col1 [196] → i5→col1 [294].
// Columns: i0→0, i1→1, i2→1, i3→0, i4→1, i5→1.
constexpr int kItems = 6;
constexpr float kHeights[kItems] = {100.0f, 60.0f, 80.0f, 120.0f, 40.0f,
                                    90.0f};
constexpr int kColumns[kItems] = {0, 1, 1, 0, 1, 1};
// (x, y, w, h) per item, column width 176, gap 8.
constexpr float kFrames[kItems][4] = {
    {0.0f, 0.0f, 176.0f, 100.0f},    // i0 col0
    {184.0f, 0.0f, 176.0f, 60.0f},   // i1 col1
    {184.0f, 68.0f, 176.0f, 80.0f},  // i2 col1 (60+8)
    {0.0f, 108.0f, 176.0f, 120.0f},  // i3 col0 (100+8)
    {184.0f, 156.0f, 176.0f, 40.0f},  // i4 col1 (148+8)
    {184.0f, 204.0f, 176.0f, 90.0f},  // i5 col1 (196+8)
};

layout::MasonryConfig masonry_config() {
  layout::MasonryConfig c;
  c.columns = 2;
  c.columnGap = 8.0f;
  c.itemGap = 8.0f;
  return c;
}

struct MasonryTree {
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId items[kItems];
};

MasonryTree build_masonry_tree(layout::Solver& solver) {
  MasonryTree t;
  t.root = solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(t.root, 360.0f, 800.0f, 0.0f);
  for (int i = 0; i < kItems; ++i) {
    t.items[i] = solver.addNode(layout::Backend::Flex, t.root);
    solver.setFlex(t.items[i], 176.0f, kHeights[i], 0.0f);
  }
  return t;
}

void estimate_all(layout::MasonryDelegate& m, const MasonryTree& t) {
  for (int i = 0; i < kItems; ++i) m.setEstimatedHeight(t.items[i], kHeights[i]);
}

}  // namespace

// ==================== Sliver section (T4, AC-LAYOUT-013 sliver half) ========

// Named case: at zero scroll offset the toolbar is fully expanded.
static bool check_sliver_offset_zero_fully_expanded() {
  layout::Solver solver;
  SliverTree t = build_sliver_tree(solver);
  layout::SliverDelegate sliver(sliver_config());
  if (!layout::registerDelegate(solver, t.root, &sliver)) return false;
  sliver.setScrollOffset(0.0f);
  if (!near(sliver.collapseFraction(), 0.0f)) return false;
  solver.solve(t.root);
  if (!near_box(solver.boxOf(t.toolbar), 0.0f, 0.0f, 360.0f, 200.0f))
    return false;
  if (!near_box(solver.boxOf(t.content), 0.0f, 200.0f, 360.0f, 600.0f))
    return false;
  // Pinned header sits at content top: y = max(200, 0) = 200.
  return near_box(solver.boxOf(t.pinned), 0.0f, 200.0f, 360.0f, 48.0f);
}

// Named case: at mid-range offset the toolbar interpolates halfway
// (offset 72 / range 144 → fraction 0.5 → height 128).
static bool check_sliver_offset_mid_interpolates() {
  layout::Solver solver;
  SliverTree t = build_sliver_tree(solver);
  layout::SliverDelegate sliver(sliver_config());
  if (!layout::registerDelegate(solver, t.root, &sliver)) return false;
  sliver.setScrollOffset(72.0f);
  if (!near(sliver.collapseFraction(), 0.5f)) return false;
  solver.solve(t.root);
  if (!near_box(solver.boxOf(t.toolbar), 0.0f, 0.0f, 360.0f, 128.0f))
    return false;
  if (!near_box(solver.boxOf(t.content), 0.0f, 128.0f, 360.0f, 600.0f))
    return false;
  // Pinned header not yet stuck: y = max(128, 72) = 128.
  return near_box(solver.boxOf(t.pinned), 0.0f, 128.0f, 360.0f, 48.0f);
}

// Named case: at/past full collapse the toolbar clamps to collapsedH and the
// pinned header sticks at the viewport top (y = scroll offset).
static bool check_sliver_offset_full_collapsed_and_pinned_sticks() {
  layout::Solver solver;
  SliverTree t = build_sliver_tree(solver);
  layout::SliverDelegate sliver(sliver_config());
  if (!layout::registerDelegate(solver, t.root, &sliver)) return false;
  sliver.setScrollOffset(200.0f);  // past the 144 range → fraction 1.
  if (!near(sliver.collapseFraction(), 1.0f)) return false;
  solver.solve(t.root);
  if (!near_box(solver.boxOf(t.toolbar), 0.0f, 0.0f, 360.0f, 56.0f))
    return false;
  if (!near_box(solver.boxOf(t.content), 0.0f, 56.0f, 360.0f, 600.0f))
    return false;
  // Pinned header stuck at viewport top: y = max(56, 200) = 200.
  return near_box(solver.boxOf(t.pinned), 0.0f, 200.0f, 360.0f, 48.0f);
}

// Named case: negative scroll offsets clamp to zero (fraction 0, no throw).
static bool check_sliver_negative_offset_clamps_to_zero() {
  layout::SliverDelegate sliver(sliver_config());
  try {
    sliver.setScrollOffset(-50.0f);
  } catch (...) {
    return false;
  }
  return near(sliver.collapseFraction(), 0.0f);
}

// Named case: offsets past the collapse range clamp the fraction to 1.0.
static bool check_sliver_past_range_clamps_to_one() {
  layout::SliverDelegate sliver(sliver_config());
  sliver.setScrollOffset(100000.0f);
  return near(sliver.collapseFraction(), 1.0f);
}

// Named case: geometry is a pure function of (config, offset) — two
// delegates with the same config and offset report the same fraction, and
// re-solving after resetting the offset reproduces the expanded frames.
static bool check_sliver_geometry_pure_function_of_offset() {
  layout::SliverDelegate a(sliver_config());
  layout::SliverDelegate b(sliver_config());
  a.setScrollOffset(72.0f);
  b.setScrollOffset(72.0f);
  if (!near(a.collapseFraction(), b.collapseFraction())) return false;

  layout::Solver solver;
  SliverTree t = build_sliver_tree(solver);
  layout::SliverDelegate sliver(sliver_config());
  if (!layout::registerDelegate(solver, t.root, &sliver)) return false;
  sliver.setScrollOffset(200.0f);
  solver.solve(t.root);
  if (!near_box(solver.boxOf(t.toolbar), 0.0f, 0.0f, 360.0f, 56.0f))
    return false;
  sliver.setScrollOffset(0.0f);
  solver.solve(t.root);
  return near_box(solver.boxOf(t.toolbar), 0.0f, 0.0f, 360.0f, 200.0f);
}

// ==================== Masonry section (T5, AC-LAYOUT-013 masonry half) ======

// Named case: greedy shortest-column placement assigns the six fixture items
// to columns {0, 1, 1, 0, 1, 1}.
static bool check_masonry_column_assignment() {
  layout::Solver solver;
  MasonryTree t = build_masonry_tree(solver);
  layout::MasonryDelegate masonry(masonry_config());
  if (!layout::registerDelegate(solver, t.root, &masonry)) return false;
  estimate_all(masonry, t);
  solver.solve(t.root);
  for (int i = 0; i < kItems; ++i) {
    if (masonry.columnOf(t.items[i]) != kColumns[i]) return false;
  }
  return true;
}

// Named case: the six fixture items resolve to exact staggered frames.
static bool check_masonry_exact_frames() {
  layout::Solver solver;
  MasonryTree t = build_masonry_tree(solver);
  layout::MasonryDelegate masonry(masonry_config());
  if (!layout::registerDelegate(solver, t.root, &masonry)) return false;
  estimate_all(masonry, t);
  solver.solve(t.root);
  for (int i = 0; i < kItems; ++i) {
    if (!near_box(solver.boxOf(t.items[i]), kFrames[i][0], kFrames[i][1],
                  kFrames[i][2], kFrames[i][3]))
      return false;
  }
  return true;
}

// Named case: columnOf on an unknown item (and on kInvalidNodeId) returns -1.
static bool check_masonry_column_of_unknown_returns_minus_one() {
  layout::Solver solver;
  MasonryTree t = build_masonry_tree(solver);
  layout::MasonryDelegate masonry(masonry_config());
  if (!layout::registerDelegate(solver, t.root, &masonry)) return false;
  estimate_all(masonry, t);
  solver.solve(t.root);
  if (masonry.columnOf(static_cast<layout::NodeId>(9999)) != -1) return false;
  return masonry.columnOf(layout::kInvalidNodeId) == -1;
}

// Named case: estimate→measure correction touches only the affected column.
// Item i1 measured 60 → 100: column 1 items at/below i1 shift down by 40;
// column 0 frames are unchanged.
static bool check_masonry_measure_correction_touches_affected_column_only() {
  layout::Solver solver;
  MasonryTree t = build_masonry_tree(solver);
  layout::MasonryDelegate masonry(masonry_config());
  if (!layout::registerDelegate(solver, t.root, &masonry)) return false;
  estimate_all(masonry, t);
  solver.solve(t.root);

  masonry.setMeasuredHeight(t.items[1], 100.0f);
  solver.solve(t.root);

  // Column 0 untouched: i0 (0,0,176,100), i3 (0,108,176,120).
  if (!near_box(solver.boxOf(t.items[0]), 0.0f, 0.0f, 176.0f, 100.0f))
    return false;
  if (!near_box(solver.boxOf(t.items[3]), 0.0f, 108.0f, 176.0f, 120.0f))
    return false;
  // Column 1 shifted: i1 (184,0,176,100), i2 y 108, i4 y 196, i5 y 244.
  if (!near_box(solver.boxOf(t.items[1]), 184.0f, 0.0f, 176.0f, 100.0f))
    return false;
  if (!near_box(solver.boxOf(t.items[2]), 184.0f, 108.0f, 176.0f, 80.0f))
    return false;
  if (!near_box(solver.boxOf(t.items[4]), 184.0f, 196.0f, 176.0f, 40.0f))
    return false;
  if (!near_box(solver.boxOf(t.items[5]), 184.0f, 244.0f, 176.0f, 90.0f))
    return false;
  // Column assignment is unchanged by the correction.
  for (int i = 0; i < kItems; ++i) {
    if (masonry.columnOf(t.items[i]) != kColumns[i]) return false;
  }
  return true;
}

// Named case: measuring every item at its estimated height is a no-op —
// frames are identical before and after (estimation replaced cleanly).
static bool check_masonry_full_measurement_matches_estimate() {
  layout::Solver solver;
  MasonryTree t = build_masonry_tree(solver);
  layout::MasonryDelegate masonry(masonry_config());
  if (!layout::registerDelegate(solver, t.root, &masonry)) return false;
  estimate_all(masonry, t);
  solver.solve(t.root);
  for (int i = 0; i < kItems; ++i)
    masonry.setMeasuredHeight(t.items[i], kHeights[i]);
  solver.solve(t.root);
  for (int i = 0; i < kItems; ++i) {
    if (!near_box(solver.boxOf(t.items[i]), kFrames[i][0], kFrames[i][1],
                  kFrames[i][2], kFrames[i][3]))
      return false;
  }
  return true;
}

// ==================== Fold section (T6, AC-LAYOUT-013 fold half) ===========
// Binds ONLY to the T6 `Interface:` line (verbatim in unit-tests/T6.md).
// Fixture: root 720x800 with three fixed children in flex row (+x):
// c0 x 0..200, c1 x 200..400, c2 x 400..720. FULL-occlusion vertical hinge
// x 290..330 lies strictly inside c1, so exactly {c1} is hinge-affected.
// The fold sequence asserts: affected set is exactly {c1}; flat→half-opened
// consume keeps unaffected boxes identical; no solved box intersects the
// FULL hinge rect (split around the hinge); a file-static JS-observer
// counter stays 0 (no JS observes the change); repeating the identical
// state is a no-op (consume returns false).
//
// ASSUMPTION (fold re-solve scope): unaffected boxes are asserted
// byte-identical (within kEps) across consumeForNextPass; the affected
// column is asserted only by the no-content-under-hinge property, never by
// exact frames — the `Interface:` line pins no post-fold geometry.
#include "layout/form_factor.h"

namespace fold_fixture {

// File-static JS-observer counter for the fold section: nothing on this
// path takes a JS callback, so this must stay 0.
int g_foldJsObserverCalls = 0;

struct FoldCols {
  layout::NodeId root = layout::kInvalidNodeId;
  layout::NodeId cols[3];
};

FoldCols build_fold_cols(layout::Solver& solver) {
  FoldCols t;
  t.root = solver.addNode(layout::Backend::Flex, layout::kInvalidNodeId);
  solver.setFlex(t.root, 720.0f, 800.0f, 0.0f);
  const float widths[3] = {200.0f, 200.0f, 320.0f};
  for (int i = 0; i < 3; ++i) {
    t.cols[i] = solver.addNode(layout::Backend::Flex, t.root);
    solver.setFlex(t.cols[i], widths[i], 800.0f, 0.0f);
  }
  return t;
}

layout::FormFactorState fold_flat() {
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

layout::FormFactorState fold_half_opened() {
  layout::FormFactorState s;
  s.posture = layout::Posture::HalfOpened;
  s.hasHinge = true;
  s.hingeX = 290.0f;
  s.hingeY = 0.0f;
  s.hingeW = 40.0f;
  s.hingeH = 800.0f;
  s.hingeOccludes = true;  // FULL occlusion → no content under the hinge.
  s.orientation = 1;
  return s;
}

bool box_intersects(float bx, float bw, float hx, float hw) {
  return bx < hx + hw && hx < bx + bw;
}

}  // namespace fold_fixture

// Named case: the FULL hinge rect selects exactly the intersecting subtree.
static bool check_fold_hinge_selects_only_intersected_column() {
  layout::Solver solver;
  fold_fixture::FoldCols t = fold_fixture::build_fold_cols(solver);
  solver.solve(t.root);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(fold_fixture::fold_half_opened());
  std::vector<layout::NodeId> got = src.affectedSubtrees(solver, t.root);
  return got.size() == 1 && got[0] == t.cols[1];
}

// Named case: flat→half-opened re-solves only hinge-affected subtrees, no
// content lands under the FULL hinge, and no JS observes the change.
static bool check_fold_resolve_scoped_no_content_under_hinge_no_js() {
  layout::Solver solver;
  fold_fixture::FoldCols t = fold_fixture::build_fold_cols(solver);
  solver.solve(t.root);
  const layout::Box before0 = solver.boxOf(t.cols[0]);
  const layout::Box before2 = solver.boxOf(t.cols[2]);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(fold_fixture::fold_flat());
  src.consumeForNextPass(solver, t.root);
  src.onWindowLayoutChanged(fold_fixture::fold_half_opened());
  if (!src.consumeForNextPass(solver, t.root)) return false;

  // Unaffected columns keep their exact frames.
  const layout::Box& after0 = solver.boxOf(t.cols[0]);
  const layout::Box& after2 = solver.boxOf(t.cols[2]);
  if (!near_box(after0, before0.x, before0.y, before0.w, before0.h))
    return false;
  if (!near_box(after2, before2.x, before2.y, before2.w, before2.h))
    return false;

  // No solved box intersects the FULL hinge rect x 290..330.
  for (int i = 0; i < 3; ++i) {
    const layout::Box& b = solver.boxOf(t.cols[i]);
    if (fold_fixture::box_intersects(b.x, b.w, 290.0f, 40.0f)) return false;
  }
  return fold_fixture::g_foldJsObserverCalls == 0;
}

// Named case: repeating the identical fold state is a no-op pass.
static bool check_fold_unchanged_state_consume_false() {
  layout::Solver solver;
  fold_fixture::FoldCols t = fold_fixture::build_fold_cols(solver);
  solver.solve(t.root);

  layout::FormFactorSource src;
  src.onWindowLayoutChanged(fold_fixture::fold_half_opened());
  if (!src.consumeForNextPass(solver, t.root)) return false;
  src.onWindowLayoutChanged(fold_fixture::fold_half_opened());
  return !src.consumeForNextPass(solver, t.root);
}

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
  // Sliver section (T4).
  RUN_CASE(check_sliver_offset_zero_fully_expanded);
  RUN_CASE(check_sliver_offset_mid_interpolates);
  RUN_CASE(check_sliver_offset_full_collapsed_and_pinned_sticks);
  RUN_CASE(check_sliver_negative_offset_clamps_to_zero);
  RUN_CASE(check_sliver_past_range_clamps_to_one);
  RUN_CASE(check_sliver_geometry_pure_function_of_offset);
  // Masonry section (T5).
  RUN_CASE(check_masonry_column_assignment);
  RUN_CASE(check_masonry_exact_frames);
  RUN_CASE(check_masonry_column_of_unknown_returns_minus_one);
  RUN_CASE(check_masonry_measure_correction_touches_affected_column_only);
  RUN_CASE(check_masonry_full_measurement_matches_estimate);
  // Fold section (T6).
  RUN_CASE(check_fold_hinge_selects_only_intersected_column);
  RUN_CASE(check_fold_resolve_scoped_no_content_under_hinge_no_js);
  RUN_CASE(check_fold_unchanged_state_consume_false);
  if (failures > 0) {
    std::printf("geometry_test: %d failing\n", failures);
    return 1;
  }
  std::printf("geometry_test: all passing\n");
  return 0;
}
