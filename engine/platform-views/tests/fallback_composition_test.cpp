// T2 contract tests — FR-PVIEWS-013 / AC-PVIEWS-020 / TC-PVIEWS-020.
// Binds ONLY to the plan T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/fallback_composition.h (+ geometry shapes) only;
// fake SdkViewId/ClipRect/Transform values and a faked virtual-display flag,
// no Activity, no window, no virtual display, no device, no network,
// no env/config reads, no filesystem, no live services.
#include <cstdio>

#include "pviews/fallback_composition.h"
#include "pviews/z_order.h"

// Case: non-exporting SDK renders via Overlay at correct pos/size/clip.
static bool check_overlay_path_placed_clipped() {
  FallbackSlot slot;
  SdkViewId id{303};  // fake non-exporting SDK view id
  ClipRect clip{10, 20, 330, 120, 4};
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, true);
  const bool ok = r.placed && r.clipApplied && r.path == FallbackPath::Overlay &&
                  r.error == FallbackError::None;
  destroyFallback(slot);
  return ok;
}

// Case: refused virtual display degrades to OffscreenCopy, placed + clipped.
static bool check_refused_vd_offscreen_copy() {
  FallbackSlot slot;
  SdkViewId id{303};
  ClipRect clip{10, 20, 330, 120, 4};
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, false);
  const bool ok = r.placed && r.clipApplied &&
                  r.path == FallbackPath::OffscreenCopy &&
                  r.error == FallbackError::None;
  destroyFallback(slot);
  return ok;
}

// Case: invalid geometry (empty clip rect) reports the error shape.
static bool check_invalid_geometry_error_shape() {
  FallbackSlot slot;
  SdkViewId id{303};
  ClipRect clip{10, 10, 5, 5, 0};  // right < left -> invalid
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, true);
  const bool ok = !r.placed && !r.clipApplied &&
                  r.error == FallbackError::InvalidGeometry;
  destroyFallback(slot);
  return ok;
}

// Case: invalid SDK view id (0) reports the error shape.
static bool check_invalid_id_zero_error_shape() {
  FallbackSlot slot;
  SdkViewId id{0};
  ClipRect clip{0, 0, 320, 50, 0};
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, true);
  const bool ok = !r.placed && !r.clipApplied &&
                  r.error == FallbackError::InvalidGeometry;
  destroyFallback(slot);
  return ok;
}

// Case: touch inside bounds routes to the SDK.
static bool check_touch_inside_sdk_hit() {
  FallbackSlot slot;
  SdkViewId id{303};
  ClipRect clip{10, 20, 330, 120, 0};
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, true);
  if (!r.placed) {
    destroyFallback(slot);
    return false;
  }
  const TouchTarget hit = routeFallbackTouch(slot, 100, 60);
  destroyFallback(slot);
  return hit == TouchTarget::SdkHit;
}

// Case: touch outside bounds routes to the engine.
static bool check_touch_outside_engine_hit() {
  FallbackSlot slot;
  SdkViewId id{303};
  ClipRect clip{10, 20, 330, 120, 0};
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, true);
  if (!r.placed) {
    destroyFallback(slot);
    return false;
  }
  const TouchTarget hit = routeFallbackTouch(slot, 500, 500);
  destroyFallback(slot);
  return hit == TouchTarget::EngineHit;
}

// Case: destroy is idempotent (double-destroy is safe).
static bool check_destroy_idempotent() {
  FallbackSlot slot;
  SdkViewId id{303};
  ClipRect clip{10, 20, 330, 120, 0};
  Transform t = Transform::identity();
  const FallbackResult r = placeFallback(slot, id, clip, t, true);
  if (!r.placed) {
    return false;
  }
  destroyFallback(slot);
  destroyFallback(slot);
  return true;
}

// Case: destroy/re-place cycles leak nothing (slot reusable).
static bool check_destroy_then_replace_reusable() {
  FallbackSlot slot;
  SdkViewId id{303};
  ClipRect clip{10, 20, 330, 120, 0};
  Transform t = Transform::identity();
  const FallbackResult first = placeFallback(slot, id, clip, t, true);
  if (!first.placed) {
    return false;
  }
  destroyFallback(slot);
  const FallbackResult second = placeFallback(slot, id, clip, t, true);
  const bool ok = second.placed && second.clipApplied &&
                  second.path == FallbackPath::Overlay &&
                  second.error == FallbackError::None;
  destroyFallback(slot);
  return ok;
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
  RUN_CASE(check_overlay_path_placed_clipped);
  RUN_CASE(check_refused_vd_offscreen_copy);
  RUN_CASE(check_invalid_geometry_error_shape);
  RUN_CASE(check_invalid_id_zero_error_shape);
  RUN_CASE(check_touch_inside_sdk_hit);
  RUN_CASE(check_touch_outside_engine_hit);
  RUN_CASE(check_destroy_idempotent);
  RUN_CASE(check_destroy_then_replace_reusable);
  if (failures > 0) {
    std::printf("fallback_composition_test: %d failing\n", failures);
    return 1;
  }
  std::printf("fallback_composition_test: all passing\n");
  return 0;
}
