#pragma once

#include "pviews/hybrid_embedding.h"
#include "pviews/z_order.h"

enum class FallbackPath {
  Overlay,
  OffscreenCopy,
};

enum class FallbackError {
  None,
  InvalidGeometry,
  OverlayRefused,
};

struct FallbackResult {
  bool placed = false;
  bool clipApplied = false;
  FallbackPath path = FallbackPath::Overlay;
  FallbackError error = FallbackError::None;
};

enum class TouchTarget {
  SdkHit,
  EngineHit,
};

struct FallbackSlot {
  bool occupied = false;
  SdkViewId id{0};
  ClipRect clip{};
  Transform transform{};
  FallbackPath path = FallbackPath::Overlay;
};

// Places the fallback overlay for `id` inside layout bounds `clip` under
// transform `t`. When `virtualDisplayAvailable` is false the virtual display
// is refused and the offscreen-copy branch is used instead. Never throws.
FallbackResult placeFallback(FallbackSlot& slot, SdkViewId id, ClipRect clip,
                             Transform t,
                             bool virtualDisplayAvailable) noexcept;

// Mirrors layout geometry: touches inside the placed clip route to the SDK,
// touches outside route to the engine. Never throws.
TouchTarget routeFallbackTouch(const FallbackSlot& slot, int x,
                               int y) noexcept;

// Tears down the fallback window; idempotent and reusable. Never throws.
void destroyFallback(FallbackSlot& slot) noexcept;
