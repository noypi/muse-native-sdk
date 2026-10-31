// T3 hermetic unit test — foldable posture mapping (NFR-GRAPHICS-002, AC-GRAPHICS-004, TC-004).
// Style: plain asserts, no dependencies, no I/O, no network, no Android headers.
// Test doubles are plain structs mirroring the Android API shapes
// (window metrics / layout info / folding feature / hinge angle) —
// never real Android headers, and never the banned NDK display-feature type.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

#include "engine/graphics/foldable/foldable.h"

static_assert(true, "foldable posture test translation unit");

static foldable::WindowMetrics ValidMetrics() {
  foldable::WindowMetrics m;
  m.bounds.x = 0.0f;
  m.bounds.y = 0.0f;
  m.bounds.width = 2208.0f;
  m.bounds.height = 1840.0f;
  return m;
}

static foldable::WindowMetrics EmptyMetrics() {
  foldable::WindowMetrics m;
  m.bounds.x = 0.0f;
  m.bounds.y = 0.0f;
  m.bounds.width = 0.0f;
  m.bounds.height = 0.0f;
  return m;
}

static foldable::WindowMetrics DegenerateMetrics() {
  foldable::WindowMetrics m;
  m.bounds.x = 0.0f;
  m.bounds.y = 0.0f;
  m.bounds.width = -100.0f;
  m.bounds.height = 1840.0f;
  return m;
}

static foldable::LayoutInfo FoldingVertical() {
  foldable::LayoutInfo i;
  i.hasFoldingFeature = true;
  i.isTabletopOrientation = false;
  return i;
}

static foldable::LayoutInfo FoldingHorizontal() {
  foldable::LayoutInfo i;
  i.hasFoldingFeature = true;
  i.isTabletopOrientation = true;
  return i;
}

static foldable::LayoutInfo NoFoldingFeature() {
  foldable::LayoutInfo i;
  i.hasFoldingFeature = false;
  i.isTabletopOrientation = false;
  return i;
}

int main() {
  using foldable::FoldPosture;
  // 1. Golden: flat — fully open device, hinge at 180 deg.
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), FoldingVertical(), 180.0f);
    assert(p == FoldPosture::kFlat);
    printf("PASS flat-at-180\n");
  }
  // 2. Golden: half-opened — vertical hinge at 90 deg.
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), FoldingVertical(), 90.0f);
    assert(p == FoldPosture::kHalfOpened);
    printf("PASS half-opened-at-90-vertical\n");
  }
  // 3. Golden: tabletop — horizontal hinge at 90 deg.
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), FoldingHorizontal(), 90.0f);
    assert(p == FoldPosture::kTabletop);
    printf("PASS tabletop-at-90-horizontal\n");
  }
  // 4. Boundary: hinge angle 0 deg (fully closed) is not a guessed posture.
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), FoldingVertical(), 0.0f);
    assert(p == FoldPosture::kUnknown);
    printf("PASS angle-0-unknown\n");
  }
  // 5. Boundary: hinge angle 360 deg (out of range) -> UNKNOWN.
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), FoldingVertical(), 360.0f);
    assert(p == FoldPosture::kUnknown);
    printf("PASS angle-360-unknown\n");
  }
  // 6. Boundary: NaN hinge angle -> UNKNOWN, never a crash.
  {
    FoldPosture p = foldable::postureFrom(
        ValidMetrics(), FoldingVertical(),
        std::numeric_limits<float>::quiet_NaN());
    assert(p == FoldPosture::kUnknown);
    printf("PASS angle-nan-unknown\n");
  }
  // 7. Boundary: negative hinge angle -> UNKNOWN.
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), FoldingVertical(), -45.0f);
    assert(p == FoldPosture::kUnknown);
    printf("PASS angle-negative-unknown\n");
  }
  // 8. Empty metrics (zero bounds) -> UNKNOWN, never a crash.
  {
    FoldPosture p =
        foldable::postureFrom(EmptyMetrics(), FoldingVertical(), 180.0f);
    assert(p == FoldPosture::kUnknown);
    printf("PASS empty-metrics-unknown\n");
  }
  // 9. Degenerate metrics (negative width) -> UNKNOWN, never a crash.
  {
    FoldPosture p =
        foldable::postureFrom(DegenerateMetrics(), FoldingVertical(), 180.0f);
    assert(p == FoldPosture::kUnknown);
    printf("PASS degenerate-metrics-unknown\n");
  }
  // 10. WindowLayoutInfo without FoldingFeature -> UNKNOWN (not FLAT).
  {
    FoldPosture p =
        foldable::postureFrom(ValidMetrics(), NoFoldingFeature(), 180.0f);
    assert(p == FoldPosture::kUnknown);
    printf("PASS no-folding-feature-unknown\n");
  }
  // 11. No-throw guarantee: all bad-bound combos return UNKNOWN.
  {
    const float kBadAngles[] = {0.0f, -1.0f, 360.0f, 720.0f,
                                std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity()};
    for (float a : kBadAngles) {
      assert(foldable::postureFrom(EmptyMetrics(), NoFoldingFeature(), a) ==
             FoldPosture::kUnknown);
    }
    printf("PASS bad-bounds-never-throw\n");
  }
  // 12. T9 re-verify (green leg): NaN / infinite hinge angles with valid
  //     metrics map to UNKNOWN — untrusted-float guard holds.
  {
    assert(foldable::postureFrom(ValidMetrics(), FoldingVertical(),
                                 std::numeric_limits<float>::quiet_NaN()) ==
           FoldPosture::kUnknown);
    assert(foldable::postureFrom(ValidMetrics(), FoldingVertical(),
                                 std::numeric_limits<float>::infinity()) ==
           FoldPosture::kUnknown);
    assert(foldable::postureFrom(ValidMetrics(), FoldingVertical(),
                                 -std::numeric_limits<float>::infinity()) ==
           FoldPosture::kUnknown);
    printf("PASS nan-inf-hinge-unknown-reverify\n");
  }
  printf("ALL FOLDABLE TESTS PASSED\n");
  return 0;
}
