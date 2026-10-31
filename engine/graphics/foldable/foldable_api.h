#ifndef ENGINE_GRAPHICS_FOLDABLE_FOLDABLE_API_H_
#define ENGINE_GRAPHICS_FOLDABLE_FOLDABLE_API_H_

// Foldable posture bridge (NFR-GRAPHICS-002, AC-GRAPHICS-004, graphics side).
// Static usage surface for the real Android posture APIs only:
//   - android.view.WindowManager.getCurrentWindowMetrics
//   - androidx.window.layout.WindowMetricsCalculator
//   - androidx.window.layout.WindowLayoutInfo
//   - androidx.window.layout.FoldingFeature
//   - android.hardware.Sensor TYPE_HINGE_ANGLE (hinge-angle source)
// Layout-owned posture splitting (LAYOUT-FOLD) lives in layout/ and is
// intentionally not duplicated here; this bridge only reports which
// platform APIs the graphics side is built against.

struct FoldableApiUsage {
  // Backed by WindowManager.getCurrentWindowMetrics.
  bool usesWindowManager = false;
  // Backed by WindowMetricsCalculator (androidx.window).
  bool usesWindowMetricsCalculator = false;
  // Backed by WindowLayoutInfo (androidx.window).
  bool usesWindowLayoutInfo = false;
  // Backed by FoldingFeature (androidx.window).
  bool usesFoldingFeature = false;
  // Backed by the TYPE_HINGE_ANGLE hinge-angle sensor source.
  bool usesHingeAngleSensor = false;
  // True when the retired pre-AndroidX display-feature naming is in use.
  // Must stay false; the run-tf.sh gate bans that naming under engine/.
  bool usesObsoleteDisplayFeature = false;
};

// Reports the static platform-API usage surface above. All five real-API
// flags are true and the obsolete-naming flag is false on this bridge.
FoldableApiUsage queryFoldableApiUsage() noexcept;

// True iff every real-API flag is set and the obsolete-naming flag is clear.
bool foldableApiCompliant(const FoldableApiUsage& usage) noexcept;

#endif  // ENGINE_GRAPHICS_FOLDABLE_FOLDABLE_API_H_
