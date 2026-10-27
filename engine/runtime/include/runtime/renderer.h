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
};

struct DrawCmd {
  PrimitiveKind kind = PrimitiveKind::Rect;
  Rect bounds;
  float radius = 0.0f;
  float opacity = 1.0f;
  std::uint32_t nodeId = 0;
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
