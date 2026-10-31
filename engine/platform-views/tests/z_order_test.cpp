// T3 contract tests — FR-PVIEWS-001 / AC-PVIEWS-002 / TC-PVIEWS-002.
// Binds ONLY to the plan T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/z_order.h only; fake SurfaceId/LayerOrder/ClipRect/
// Transform values, no ASurfaceControl, no device, no network, no env/config
// reads, no filesystem, no live services, no device touch.
#include <cstdio>

#include "pviews/z_order.h"

// Case: valid placement above engine content reports placed + clip applied.
static bool check_place_above_applies_clip() {
  SurfaceId id{1};
  LayerOrder order{Layer::ABOVE};
  ClipRect clip{0, 0, 100, 100, 8};
  Transform t = Transform::identity();
  const ZOrderResult r = placeSurface(id, order, clip, t);
  return r.placed && r.layer > 0 && r.clipApplied;
}

// Case: valid placement below engine content reports placed.
static bool check_place_below_reports_placed() {
  SurfaceId id{2};
  LayerOrder order{Layer::BELOW};
  ClipRect clip{0, 0, 50, 50, 0};
  Transform t = Transform::identity();
  const ZOrderResult r = placeSurface(id, order, clip, t);
  return r.placed && r.layer < 0 && r.clipApplied;
}

// Case: invalid geometry (empty clip rect) reports error shape, never throws.
static bool check_invalid_geometry_error_shape() {
  SurfaceId id{3};
  LayerOrder order{Layer::ABOVE};
  ClipRect clip{10, 10, 5, 5, 0};  // right < left -> invalid
  Transform t = Transform::identity();
  const ZOrderResult r = placeSurface(id, order, clip, t);
  return !r.placed && r.layer == -1 && !r.clipApplied;
}

// Case: scroll offset transform keeps placement and clip.
static bool check_scroll_transform_keeps_clip() {
  SurfaceId id{4};
  LayerOrder order{Layer::ABOVE};
  ClipRect clip{0, 100, 200, 300, 4};
  Transform t = Transform::translated(0, -100);
  const ZOrderResult r = placeSurface(id, order, clip, t);
  return r.placed && r.clipApplied;
}

// Case: zero-area clip rect is invalid -> error shape.
static bool check_zero_area_clip_invalid() {
  SurfaceId id{5};
  LayerOrder order{Layer::BELOW};
  ClipRect clip{0, 0, 0, 0, 0};
  Transform t = Transform::identity();
  const ZOrderResult r = placeSurface(id, order, clip, t);
  return !r.placed && r.layer == -1 && !r.clipApplied;
}

// Case: invalid surface id (0) reports error shape.
static bool check_invalid_surface_id_error_shape() {
  SurfaceId id{0};
  LayerOrder order{Layer::ABOVE};
  ClipRect clip{0, 0, 100, 100, 0};
  Transform t = Transform::identity();
  const ZOrderResult r = placeSurface(id, order, clip, t);
  return !r.placed && r.layer == -1 && !r.clipApplied;
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
  RUN_CASE(check_place_above_applies_clip);
  RUN_CASE(check_place_below_reports_placed);
  RUN_CASE(check_invalid_geometry_error_shape);
  RUN_CASE(check_scroll_transform_keeps_clip);
  RUN_CASE(check_zero_area_clip_invalid);
  RUN_CASE(check_invalid_surface_id_error_shape);
  if (failures > 0) {
    std::printf("z_order_test: %d failing\n", failures);
    return 1;
  }
  std::printf("z_order_test: all passing\n");
  return 0;
}
