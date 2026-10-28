// T3 contract tests — FR-LAYOUT-014 / AC-LAYOUT-041 (masonry grids and sliver
// lists split around a FULL-occlusion hinge automatically).
// Maps to TC-LAYOUT-041 (`engine/layout/tests/hinge_split_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line (reproduced verbatim in
// unit-tests/T3.md):
//   `bool splitAroundHinge(const Rect& viewport, const Rect& hinge,
//    OcclusionType occlusion, Rect* outA, Rect* outB) noexcept;`
//
// ASSUMPTION (Rect): `layout::Rect` is an aggregate of four floats read as
// x/y/w/h (per batch-1 form_factor.h); cases assign/compare those fields.
// ASSUMPTION (enumerators): `OcclusionType::None/Full` PascalCase, matching
// batch-1 `layout/form_factor.h`.
// ASSUMPTION (split axis): the Interface line pins no axis rule. These cases
// test the defensible geometry — a hinge spanning the full viewport height
// (vertical band, w < h) splits into left/right panes; a hinge spanning the
// full viewport width (horizontal band, h < w) splits into top/bottom panes.
// Panes abut the hinge edges exactly, so zero content lands inside the hinge
// rect (TC-LAYOUT-041 core).
// ASSUMPTION (null outputs): passing nullptr for outA/outB returns false
// without writing through the null pointer (never throws / never crashes
// half of the noexcept contract under hostile input).
#include <cstdio>
#include <utility>

#include "layout/form_factor.h"
#include "layout/hinge_split.h"

namespace {

// Epsilon for float comparisons; fixture values are exactly representable.
constexpr float kEps = 1e-4f;

bool near(float a, float b) { return (a > b ? a - b : b - a) <= kEps; }

bool rectNear(const layout::Rect& a, const layout::Rect& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) &&
         near(a.h, b.h);
}

// True when r has empty (non-positive) area.
bool rectEmpty(const layout::Rect& r) { return r.w <= 0.0f || r.h <= 0.0f; }

// True when a and b overlap with nonzero area.
bool rectsOverlap(const layout::Rect& a, const layout::Rect& b) {
  const float x0 = a.x > b.x ? a.x : b.x;
  const float y0 = a.y > b.y ? a.y : b.y;
  const float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
  const float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
  return x1 > x0 + kEps && y1 > y0 + kEps;
}

constexpr layout::Rect kViewport{0.0f, 0.0f, 720.0f, 800.0f};
constexpr layout::Rect kVerticalHinge{350.0f, 0.0f, 20.0f, 800.0f};
constexpr layout::Rect kHorizontalHinge{0.0f, 390.0f, 720.0f, 20.0f};
constexpr layout::Rect kSentinel{1.0f, 2.0f, 3.0f, 4.0f};

}  // namespace

// Compile-time: the split helper never throws (Interface line pins noexcept).
static_assert(noexcept(layout::splitAroundHinge(
                  std::declval<const layout::Rect&>(),
                  std::declval<const layout::Rect&>(),
                  std::declval<layout::OcclusionType>(),
                  static_cast<layout::Rect*>(nullptr),
                  static_cast<layout::Rect*>(nullptr))),
              "splitAroundHinge must be noexcept");

// Named case (TC-LAYOUT-041 core, vertical): a FULL-occlusion vertical hinge
// splits the viewport into left/right panes abutting the hinge edges, with
// zero content inside the hinge rect.
static bool check_full_vertical_hinge_splits_left_right() {
  layout::Rect a{}, b{};
  if (!layout::splitAroundHinge(kViewport, kVerticalHinge,
                                layout::OcclusionType::Full, &a, &b)) {
    return false;
  }
  if (!rectNear(a, layout::Rect{0.0f, 0.0f, 350.0f, 800.0f})) return false;
  if (!rectNear(b, layout::Rect{370.0f, 0.0f, 350.0f, 800.0f})) return false;
  // Zero content inside the hinge rect: neither pane overlaps the hinge.
  if (rectsOverlap(a, kVerticalHinge)) return false;
  if (rectsOverlap(b, kVerticalHinge)) return false;
  return true;
}

// Named case (TC-LAYOUT-041 core, horizontal): a FULL-occlusion horizontal
// hinge splits the viewport into top/bottom panes with zero content inside
// the hinge rect.
static bool check_full_horizontal_hinge_splits_top_bottom() {
  layout::Rect a{}, b{};
  if (!layout::splitAroundHinge(kViewport, kHorizontalHinge,
                                layout::OcclusionType::Full, &a, &b)) {
    return false;
  }
  if (!rectNear(a, layout::Rect{0.0f, 0.0f, 720.0f, 390.0f})) return false;
  if (!rectNear(b, layout::Rect{0.0f, 410.0f, 720.0f, 390.0f})) return false;
  if (rectsOverlap(a, kHorizontalHinge)) return false;
  if (rectsOverlap(b, kHorizontalHinge)) return false;
  return true;
}

// Named case (Interface error shape): occlusion NONE lays out full-width —
// returns false and leaves both outputs untouched.
static bool check_none_occlusion_returns_false_outputs_untouched() {
  layout::Rect a = kSentinel, b = kSentinel;
  if (layout::splitAroundHinge(kViewport, kVerticalHinge,
                               layout::OcclusionType::None, &a, &b)) {
    return false;
  }
  return rectNear(a, kSentinel) && rectNear(b, kSentinel);
}

// Named case (Interface error shape): an empty hinge rect (zero width)
// returns false and leaves both outputs untouched, even under FULL.
static bool check_empty_hinge_returns_false_outputs_untouched() {
  const layout::Rect empty{350.0f, 0.0f, 0.0f, 800.0f};
  layout::Rect a = kSentinel, b = kSentinel;
  if (layout::splitAroundHinge(kViewport, empty, layout::OcclusionType::Full,
                               &a, &b)) {
    return false;
  }
  if (!rectNear(a, kSentinel) || !rectNear(b, kSentinel)) return false;
  // Zero-area hinge (w == 0 and h == 0) likewise never splits.
  const layout::Rect point{100.0f, 100.0f, 0.0f, 0.0f};
  a = kSentinel;
  b = kSentinel;
  if (layout::splitAroundHinge(kViewport, point, layout::OcclusionType::Full,
                               &a, &b)) {
    return false;
  }
  return rectNear(a, kSentinel) && rectNear(b, kSentinel);
}

// Named case (Interface error shape): negative-size hinge rect counts as
// empty — returns false, outputs untouched.
static bool check_negative_hinge_returns_false_outputs_untouched() {
  const layout::Rect inverted{350.0f, 0.0f, -20.0f, 800.0f};
  layout::Rect a = kSentinel, b = kSentinel;
  if (layout::splitAroundHinge(kViewport, inverted,
                               layout::OcclusionType::Full, &a, &b)) {
    return false;
  }
  return rectNear(a, kSentinel) && rectNear(b, kSentinel);
}

// Named case (boundary): null output pointers return false without crashing
// (noexcept under hostile input; nothing is written through null).
static bool check_null_outputs_return_false_without_crashing() {
  bool threw = false;
  bool rNullB = true;
  bool rNullA = true;
  bool rBothNull = true;
  try {
    layout::Rect b = kSentinel;
    rNullB = layout::splitAroundHinge(kViewport, kVerticalHinge,
                                      layout::OcclusionType::Full, nullptr,
                                      &b);
    layout::Rect a = kSentinel;
    rNullA = layout::splitAroundHinge(kViewport, kVerticalHinge,
                                      layout::OcclusionType::Full, &a,
                                      nullptr);
    rBothNull = layout::splitAroundHinge(kViewport, kVerticalHinge,
                                         layout::OcclusionType::Full, nullptr,
                                         nullptr);
  } catch (...) {
    threw = true;
  }
  return !threw && !rNullB && !rNullA && !rBothNull;
}

// Named case (boundary): split panes tile the viewport minus the hinge —
// pane areas plus hinge area equal the viewport area (no gaps, no content
// under the hinge).
static bool check_panes_tile_viewport_minus_hinge() {
  layout::Rect a{}, b{};
  if (!layout::splitAroundHinge(kViewport, kVerticalHinge,
                                layout::OcclusionType::Full, &a, &b)) {
    return false;
  }
  if (rectEmpty(a) || rectEmpty(b)) return false;
  const float tiled = a.w * a.h + b.w * b.h +
                      kVerticalHinge.w * kVerticalHinge.h;
  if (!near(tiled, kViewport.w * kViewport.h)) return false;
  if (!layout::splitAroundHinge(kViewport, kHorizontalHinge,
                                layout::OcclusionType::Full, &a, &b)) {
    return false;
  }
  const float tiledH =
      a.w * a.h + b.w * b.h + kHorizontalHinge.w * kHorizontalHinge.h;
  return near(tiledH, kViewport.w * kViewport.h);
}

// Named case: the helper is deterministic — same inputs yield identical
// panes on repeat calls.
static bool check_repeat_calls_are_deterministic() {
  layout::Rect a1{}, b1{}, a2{}, b2{};
  if (!layout::splitAroundHinge(kViewport, kVerticalHinge,
                                layout::OcclusionType::Full, &a1, &b1)) {
    return false;
  }
  if (!layout::splitAroundHinge(kViewport, kVerticalHinge,
                                layout::OcclusionType::Full, &a2, &b2)) {
    return false;
  }
  return rectNear(a1, a2) && rectNear(b1, b2);
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
  RUN_CASE(check_full_vertical_hinge_splits_left_right);
  RUN_CASE(check_full_horizontal_hinge_splits_top_bottom);
  RUN_CASE(check_none_occlusion_returns_false_outputs_untouched);
  RUN_CASE(check_empty_hinge_returns_false_outputs_untouched);
  RUN_CASE(check_negative_hinge_returns_false_outputs_untouched);
  RUN_CASE(check_null_outputs_return_false_without_crashing);
  RUN_CASE(check_panes_tile_viewport_minus_hinge);
  RUN_CASE(check_repeat_calls_are_deterministic);
  if (failures > 0) {
    std::printf("hinge_split_test: %d failing\n", failures);
    return 1;
  }
  std::printf("hinge_split_test: all passing\n");
  return 0;
}
