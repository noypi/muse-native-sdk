#ifndef MUSE_NATIVE_SDK_ENGINE_GRAPHICS_FOLDABLE_FOLDABLE_H_
#define MUSE_NATIVE_SDK_ENGINE_GRAPHICS_FOLDABLE_FOLDABLE_H_

// Foldable posture signal (NFR-GRAPHICS-002, AC-GRAPHICS-004).
// Plain structs mirror the Android WindowManager shapes
// (WindowMetrics / WindowLayoutInfo / FoldingFeature + hinge angle)
// without including any Android header.

namespace foldable {

enum class FoldPosture { kFlat, kHalfOpened, kTabletop, kBook, kUnknown };

struct Bounds {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

struct WindowMetrics {
  Bounds bounds;
};

struct LayoutInfo {
  bool hasFoldingFeature = false;
  bool isTabletopOrientation = false;
};

// Maps (metrics, layout info, hinge angle in degrees) to a posture.
// Never throws: invalid bounds or angles return kUnknown.
FoldPosture postureFrom(const WindowMetrics& m, const LayoutInfo& i,
                        float hingeAngleDeg) noexcept;

}  // namespace foldable

#endif  // MUSE_NATIVE_SDK_ENGINE_GRAPHICS_FOLDABLE_FOLDABLE_H_
