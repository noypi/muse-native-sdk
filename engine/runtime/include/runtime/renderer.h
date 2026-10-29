#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/vulkan_baseline.h"

namespace runtime {

struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

enum class PrimitiveKind {
  Rect,
  RoundedRect,
  Image,
  Text,
  StateLayer,
  Ripple,
  ElevationShadow,
  CircleArc,
  VectorIcon,
  ShapedText,
  OverlayScrim,
};

struct PrimitivePayload {
  std::uint32_t stateColor = 0;
  float stateOpacity = 1.0f;
  Rect rippleBounds;
  float rippleProgress = 0.0f;
  std::uint32_t rippleColor = 0;
  float elevationDp = 0.0f;
  float shadowBlur = 0.0f;
  std::uint32_t shadowColor = 0;
  float arcStartDeg = 0.0f;
  float arcSweepDeg = 0.0f;
  float strokeWidth = 0.0f;
  std::uint32_t arcColor = 0;
  std::uint32_t assetId = 0;
  std::uint32_t tintColor = 0;
  std::uint32_t glyphRunId = 0;
  std::uint32_t textColor = 0;
  std::uint32_t scrimColor = 0;
  float scrimAlpha = 0.0f;
};

struct DrawCmd {
  PrimitiveKind kind = PrimitiveKind::Rect;
  Rect bounds;
  float radius = 0.0f;
  float opacity = 1.0f;
  std::uint32_t nodeId = 0;
  PrimitivePayload payload;
};

enum class DrawError {
  None,
  UnknownPrimitive,
  UnsupportedBaseline,
};

class Swapchain {
 public:
  virtual ~Swapchain() = default;
  virtual void submit(const DrawCmd& cmd) = 0;
};

class Renderer {
 public:
  explicit Renderer(DeviceCaps caps);

  DrawError draw(const std::vector<DrawCmd>& cmds, Swapchain& target);
  std::string lastError() const;

 private:
  DeviceCaps caps_;
  std::string lastError_;
};

}  // namespace runtime
