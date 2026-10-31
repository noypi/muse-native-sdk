// T1 hermetic unit test — foldable posture API usage (NFR-GRAPHICS-002, AC-GRAPHICS-004).
// Style: plain asserts, no dependencies, no I/O, no network.
// Expectations derive ONLY from AC-GRAPHICS-004: foldable posture uses the real
// Android APIs (WindowManager.getCurrentWindowMetrics() / androidx.window
// WindowMetricsCalculator, WindowLayoutInfo, FoldingFeature with hinge angle
// from SensorManager TYPE_HINGE_ANGLE) and never the obsolete ADisplayFeature
// naming. Layout-owned splitting (LAYOUT-FOLD) is out of scope and unasserted.
#include <cassert>
#include <cstdio>

#include "engine/graphics/foldable/foldable_api.h"

int main() {
  // 1. The bridge reports use of every real API and no obsolete naming.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    assert(u.usesWindowManager);
    assert(u.usesWindowMetricsCalculator);
    assert(u.usesWindowLayoutInfo);
    assert(u.usesFoldingFeature);
    assert(u.usesHingeAngleSensor);
    assert(!u.usesObsoleteDisplayFeature);
    printf("PASS bridge-uses-only-real-apis\n");
  }
  // 2. Full real-API usage with no obsolete naming is compliant.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    assert(foldableApiCompliant(u));
    printf("PASS full-real-api-usage-compliant\n");
  }
  // 3. Any obsolete ADisplayFeature usage is non-compliant (ban, even when
  //    every real API is also reported).
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    u.usesObsoleteDisplayFeature = true;
    assert(!foldableApiCompliant(u));
    printf("PASS obsolete-display-feature-banned\n");
  }
  // 4. Missing WindowManager.getCurrentWindowMetrics is non-compliant.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    u.usesWindowManager = false;
    assert(!foldableApiCompliant(u));
    printf("PASS missing-window-manager-noncompliant\n");
  }
  // 5. Missing WindowMetricsCalculator is non-compliant.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    u.usesWindowMetricsCalculator = false;
    assert(!foldableApiCompliant(u));
    printf("PASS missing-metrics-calculator-noncompliant\n");
  }
  // 6. Missing WindowLayoutInfo is non-compliant.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    u.usesWindowLayoutInfo = false;
    assert(!foldableApiCompliant(u));
    printf("PASS missing-layout-info-noncompliant\n");
  }
  // 7. Missing FoldingFeature is non-compliant.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    u.usesFoldingFeature = false;
    assert(!foldableApiCompliant(u));
    printf("PASS missing-folding-feature-noncompliant\n");
  }
  // 8. Missing TYPE_HINGE_ANGLE sensor is non-compliant.
  {
    FoldableApiUsage u = queryFoldableApiUsage();
    u.usesHingeAngleSensor = false;
    assert(!foldableApiCompliant(u));
    printf("PASS missing-hinge-angle-noncompliant\n");
  }
  // 9. Zeroed usage (no APIs at all) is non-compliant.
  {
    FoldableApiUsage u{};
    assert(!foldableApiCompliant(u));
    printf("PASS zeroed-usage-noncompliant\n");
  }
  printf("ALL FOLDABLE_API TESTS PASSED\n");
  return 0;
}
