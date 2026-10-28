// T4 contract tests — FR-LAYOUT-014 / AC-LAYOUT-042 (tabletop posture
// repositions content and media and triggers the corresponding motion
// behavior; layout owns geometry only — motion curves live in
// specs-animation-motion.md and are consumed, not implemented).
// Maps to TC-LAYOUT-042 (`engine/layout/tests/tabletop_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T4 `Interface:` line (reproduced verbatim in
// unit-tests/T4.md):
//   `struct TabletopLayout { Rect media; Rect content; bool active; };
//    TabletopLayout solveTabletop(const Rect& viewport, FoldState state,
//    FoldOrientation orientation) noexcept;`
//
// ASSUMPTION (Rect/enumerators): `layout::Rect` x/y/w/h aggregate and
// PascalCase `FoldState::Flat/HalfOpened/Unknown`,
// `FoldOrientation::Vertical/Horizontal` per batch-1 form_factor.h.
// ASSUMPTION (horizontal geometry): HALF_OPENED + Horizontal yields
// media-above/content-below panes split at the viewport mid-height (per the
// Acceptance line); the fold seam itself carries no occluding area here.
// ASSUMPTION (vertical geometry): the Interface/Acceptance lines pin only
// the horizontal fold. The defensible mirror is HALF_OPENED + Vertical
// yields media-left/content-right panes split at the viewport mid-width.
// ASSUMPTION (inactive shape): when `active` is false (FLAT or Unknown) the
// layout is a single pane — `content` fills the viewport and `media` is an
// empty rect. Only `active == false` is contract-pinned; the pane choice is
// the assumption.
// ASSUMPTION (motion hook): the posture transition observable to the motion
// contract is the `active` flag flip (false→true on HALF_OPENED, true→false
// on FLAT); no motion curve or callback is asserted here (geometry only).
#include <cstdio>
#include <utility>

#include "layout/form_factor.h"
#include "layout/tabletop.h"

namespace {

// Epsilon for float comparisons; fixture values are exactly representable.
constexpr float kEps = 1e-4f;

bool near(float a, float b) { return (a > b ? a - b : b - a) <= kEps; }

bool rectNear(const layout::Rect& a, const layout::Rect& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) &&
         near(a.h, b.h);
}

bool rectEmpty(const layout::Rect& r) { return r.w <= 0.0f || r.h <= 0.0f; }

bool rectsOverlap(const layout::Rect& a, const layout::Rect& b) {
  const float x0 = a.x > b.x ? a.x : b.x;
  const float y0 = a.y > b.y ? a.y : b.y;
  const float x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
  const float y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
  return x1 > x0 + kEps && y1 > y0 + kEps;
}

bool rectInside(const layout::Rect& inner, const layout::Rect& outer) {
  return inner.x + kEps >= outer.x && inner.y + kEps >= outer.y &&
         inner.x + inner.w <= outer.x + outer.w + kEps &&
         inner.y + inner.h <= outer.y + outer.h + kEps;
}

constexpr layout::Rect kViewport{0.0f, 0.0f, 720.0f, 800.0f};

}  // namespace

// Compile-time: tabletop solving never throws (Interface line pins noexcept;
// the motion contract consumes the result, so no callback can throw through).
static_assert(noexcept(layout::solveTabletop(
                  std::declval<const layout::Rect&>(),
                  std::declval<layout::FoldState>(),
                  std::declval<layout::FoldOrientation>())),
              "solveTabletop must be noexcept");

// Named case (TC-LAYOUT-042 core): HALF_OPENED + Horizontal yields active
// media-above/content-below panes tiling the viewport.
static bool check_half_opened_horizontal_is_media_above() {
  const layout::TabletopLayout t = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Horizontal);
  if (!t.active) return false;
  if (!rectNear(t.media, layout::Rect{0.0f, 0.0f, 720.0f, 400.0f})) {
    return false;
  }
  if (!rectNear(t.content, layout::Rect{0.0f, 400.0f, 720.0f, 400.0f})) {
    return false;
  }
  // Media sits strictly above content (the posture transition the motion
  // contract consumes) and the panes do not overlap.
  if (!(t.media.y + t.media.h <= t.content.y + kEps)) return false;
  if (rectsOverlap(t.media, t.content)) return false;
  if (!rectInside(t.media, kViewport)) return false;
  return rectInside(t.content, kViewport);
}

// Named case: HALF_OPENED + Vertical yields active media-left/content-right
// panes (mirror assumption; only the horizontal fold is contract-pinned).
static bool check_half_opened_vertical_is_media_left() {
  const layout::TabletopLayout t = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Vertical);
  if (!t.active) return false;
  if (!rectNear(t.media, layout::Rect{0.0f, 0.0f, 360.0f, 800.0f})) {
    return false;
  }
  if (!rectNear(t.content, layout::Rect{360.0f, 0.0f, 360.0f, 800.0f})) {
    return false;
  }
  if (!(t.media.x + t.media.w <= t.content.x + kEps)) return false;
  if (rectsOverlap(t.media, t.content)) return false;
  return true;
}

// Named case (TC-LAYOUT-042 core): FLAT yields a single-pane inactive layout
// regardless of orientation.
static bool check_flat_is_single_pane_inactive() {
  for (const layout::FoldOrientation o :
       {layout::FoldOrientation::Horizontal, layout::FoldOrientation::Vertical}) {
    const layout::TabletopLayout t =
        layout::solveTabletop(kViewport, layout::FoldState::Flat, o);
    if (t.active) return false;
    if (!rectNear(t.content, kViewport)) return false;
    if (!rectEmpty(t.media)) return false;
  }
  return true;
}

// Named case (Interface error shape): Unknown state is inactive — never
// half-places panes.
static bool check_unknown_state_is_inactive() {
  const layout::TabletopLayout t = layout::solveTabletop(
      kViewport, layout::FoldState::Unknown,
      layout::FoldOrientation::Horizontal);
  if (t.active) return false;
  if (!rectNear(t.content, kViewport)) return false;
  return rectEmpty(t.media);
}

// Named case (boundary): active panes tile the viewport — media plus content
// areas equal the viewport area (no gaps, no overlap, nothing off-viewport).
static bool check_active_panes_tile_viewport() {
  const layout::TabletopLayout h = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Horizontal);
  if (!h.active) return false;
  if (!near(h.media.w * h.media.h + h.content.w * h.content.h,
            kViewport.w * kViewport.h)) {
    return false;
  }
  const layout::TabletopLayout v = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Vertical);
  if (!v.active) return false;
  return near(v.media.w * v.media.h + v.content.w * v.content.h,
              kViewport.w * kViewport.h);
}

// Named case (motion-hook half of the contract): the posture transition is
// observable as the `active` flag flip — FLAT→HALF_OPENED activates,
// HALF_OPENED→FLAT deactivates — with no other state carried.
static bool check_active_flag_tracks_posture_transition() {
  const layout::TabletopLayout flat = layout::solveTabletop(
      kViewport, layout::FoldState::Flat, layout::FoldOrientation::Horizontal);
  const layout::TabletopLayout half = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Horizontal);
  const layout::TabletopLayout back = layout::solveTabletop(
      kViewport, layout::FoldState::Flat, layout::FoldOrientation::Horizontal);
  return !flat.active && half.active && !back.active;
}

// Named case: the solver is deterministic — same inputs yield identical
// panes on repeat calls.
static bool check_repeat_calls_are_deterministic() {
  const layout::TabletopLayout a = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Horizontal);
  const layout::TabletopLayout b = layout::solveTabletop(
      kViewport, layout::FoldState::HalfOpened,
      layout::FoldOrientation::Horizontal);
  return a.active == b.active && rectNear(a.media, b.media) &&
         rectNear(a.content, b.content);
}

// Named case: extreme viewport values never throw and stay queryable
// (never-throws half of the contract under hostile input).
static bool check_extreme_viewport_never_throws() {
  bool threw = false;
  try {
    const layout::Rect huge{-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f};
    const layout::TabletopLayout t = layout::solveTabletop(
        huge, layout::FoldState::HalfOpened,
        layout::FoldOrientation::Horizontal);
    (void)t.active;
    const layout::Rect empty{0.0f, 0.0f, 0.0f, 0.0f};
    const layout::TabletopLayout e = layout::solveTabletop(
        empty, layout::FoldState::Flat, layout::FoldOrientation::Vertical);
    if (e.active) return false;
  } catch (...) {
    threw = true;
  }
  return !threw;
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
  RUN_CASE(check_half_opened_horizontal_is_media_above);
  RUN_CASE(check_half_opened_vertical_is_media_left);
  RUN_CASE(check_flat_is_single_pane_inactive);
  RUN_CASE(check_unknown_state_is_inactive);
  RUN_CASE(check_active_panes_tile_viewport);
  RUN_CASE(check_active_flag_tracks_posture_transition);
  RUN_CASE(check_repeat_calls_are_deterministic);
  RUN_CASE(check_extreme_viewport_never_throws);
  if (failures > 0) {
    std::printf("tabletop_test: %d failing\n", failures);
    return 1;
  }
  std::printf("tabletop_test: all passing\n");
  return 0;
}
