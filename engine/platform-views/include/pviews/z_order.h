#pragma once

#include <cstdint>

struct SurfaceId {
  std::uint64_t value = 0;
};

enum class Layer {
  ABOVE,
  BELOW,
};

struct LayerOrder {
  Layer layer = Layer::ABOVE;
};

struct ClipRect {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
  int cornerRadius = 0;
};

struct Transform {
  float dx = 0.0f;
  float dy = 0.0f;

  static Transform identity() { return Transform{0.0f, 0.0f}; }
  static Transform translated(float x, float y) { return Transform{x, y}; }
};

struct ZOrderResult {
  bool placed = false;
  int layer = -1;
  bool clipApplied = false;
};

// Reparents the sub-surface for `id` above/below the Vulkan engine surface
// according to `order`, applying `clip` (hardware clip regions/corner radii
// on API 31+, offscreen fallback for complex blending) under transform `t`
// (scroll/transform/resize). Returns {false, -1, false} on invalid geometry.
ZOrderResult placeSurface(SurfaceId id, LayerOrder order, ClipRect clip,
                         Transform t);
