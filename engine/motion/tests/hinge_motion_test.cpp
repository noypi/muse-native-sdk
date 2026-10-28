// T7 contract tests — FR-MOTION-012 / AC-MOTION-038.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: pure
// functions of explicit inputs, no wall clock, no randomness.
// Binds to the T7 `Interface:` line (reproduced verbatim in unit-tests/T7.md).
//
// Greenfield `motion/large_motion.h` — this file must FAIL TO COMPILE until
// the coder adds that header (red state).
//
// ASSUMPTION (hinge-line injection convention): the plan/spec do not pin how
// the tests know the hinge line. These tests inject it as a test-chosen
// vertical line `kHingeX` and place boxes relative to it: left boxes have
// (x + w) <= kHingeX, right boxes have x >= kHingeX, spanning boxes have
// x < kHingeX < x + w. "Panes on one side" means both fromPane and toPane
// are wholly left or wholly right of kHingeX.
#include <cmath>
#include <cstdio>
#include <vector>

#include "motion/layout_adapter.h"
#include "motion/large_motion.h"

namespace {

const float kHingeX = 500.0f;
const float kEps = 1e-5f;

bool near(float a, float b) { return std::fabs(a - b) <= kEps; }

motion::Box box(float x, float y, float w, float h) {
  motion::Box b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  return b;
}

bool boxNear(const motion::Box& a, const motion::Box& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h);
}

// One side of the injected hinge line: wholly left or wholly right.
bool onOneSide(const motion::Box& b) {
  bool left = (b.x + b.w) <= kHingeX + kEps;
  bool right = b.x >= kHingeX - kEps;
  return left || right;
}

// FULL occlusion sweep across all postures: crossing input must come back
// non-crossing with panes on one side of the hinge line.
bool check_full_flat_no_cross() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);   // left
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);   // right
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::Flat, motion::HingeOcclusion::Full, a, b);
  return m.crossHinge == false && onOneSide(m.fromPane) && onOneSide(m.toPane);
}

bool check_full_tabletop_no_cross() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::Tabletop, motion::HingeOcclusion::Full, a, b);
  return m.crossHinge == false && onOneSide(m.fromPane) && onOneSide(m.toPane);
}

bool check_full_halfOpened_no_cross() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::HalfOpened, motion::HingeOcclusion::Full, a, b);
  return m.crossHinge == false && onOneSide(m.fromPane) && onOneSide(m.toPane);
}

bool check_full_folded_no_cross() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::Folded, motion::HingeOcclusion::Full, a, b);
  return m.crossHinge == false && onOneSide(m.fromPane) && onOneSide(m.toPane);
}

// FULL occlusion with same-side input stays non-crossing.
bool check_full_same_side_no_cross() {
  motion::Box a = box(50.0f, 100.0f, 150.0f, 300.0f);    // left
  motion::Box b = box(200.0f, 100.0f, 150.0f, 300.0f);   // left
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::Tabletop, motion::HingeOcclusion::Full, a, b);
  return m.crossHinge == false && onOneSide(m.fromPane) && onOneSide(m.toPane);
}

// Tabletop + Partial selects the pane-move variant: cross-pane request is
// preserved (crossHinge true) and endpoints round-trip.
bool check_tabletop_partial_pane_move_variant() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::Tabletop, motion::HingeOcclusion::Partial, a, b);
  return m.crossHinge == true && boxNear(m.fromPane, a) && boxNear(m.toPane, b);
}

// HalfOpened + Partial selects the pane-move variant.
bool check_halfOpened_partial_pane_move_variant() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::HalfOpened, motion::HingeOcclusion::Partial, a, b);
  return m.crossHinge == true && boxNear(m.fromPane, a) && boxNear(m.toPane, b);
}

// HalfOpened + None allows crossing.
bool check_halfOpened_none_allows_crossing() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::HalfOpened, motion::HingeOcclusion::None, a, b);
  return m.crossHinge == true && boxNear(m.fromPane, a) && boxNear(m.toPane, b);
}

// Tabletop + None allows crossing.
bool check_tabletop_none_allows_crossing() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);
  motion::PaneMove m = motion::motion_paneMove(
      motion::Posture::Tabletop, motion::HingeOcclusion::None, a, b);
  return m.crossHinge == true && boxNear(m.fromPane, a) && boxNear(m.toPane, b);
}

// Posture-differentiating case: identical crossing boxes under Partial
// occlusion give different documented outputs per the in-code rule —
// Flat/Folded snap `to` onto the source side (crossHinge false) while
// Tabletop/HalfOpened preserve endpoints (crossHinge from placement).
bool check_flat_vs_tabletop_partial_differ() {
  motion::Box a = box(100.0f, 100.0f, 200.0f, 300.0f);   // left
  motion::Box b = box(600.0f, 100.0f, 200.0f, 300.0f);   // right
  motion::PaneMove flat = motion::motion_paneMove(
      motion::Posture::Flat, motion::HingeOcclusion::Partial, a, b);
  motion::PaneMove table = motion::motion_paneMove(
      motion::Posture::Tabletop, motion::HingeOcclusion::Partial, a, b);
  if (!(flat.crossHinge == false)) return false;
  if (!(table.crossHinge == true)) return false;
  // Tabletop preserves endpoints verbatim; Flat snaps `to` onto the left.
  if (!boxNear(table.fromPane, a)) return false;
  if (!boxNear(table.toPane, b)) return false;
  if (!boxNear(flat.toPane, box(300.0f, 100.0f, 200.0f, 300.0f)))
    return false;
  // The two postures observably differ on the same input.
  return !boxNear(flat.toPane, table.toPane);
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
  RUN_CASE(check_full_flat_no_cross);
  RUN_CASE(check_full_tabletop_no_cross);
  RUN_CASE(check_full_halfOpened_no_cross);
  RUN_CASE(check_full_folded_no_cross);
  RUN_CASE(check_full_same_side_no_cross);
  RUN_CASE(check_tabletop_partial_pane_move_variant);
  RUN_CASE(check_halfOpened_partial_pane_move_variant);
  RUN_CASE(check_halfOpened_none_allows_crossing);
  RUN_CASE(check_tabletop_none_allows_crossing);
  RUN_CASE(check_flat_vs_tabletop_partial_differ);
  if (failures > 0) {
    std::printf("hinge_motion_test: %d failing\n", failures);
    return 1;
  }
  std::printf("hinge_motion_test: all passing\n");
  return 0;
}
