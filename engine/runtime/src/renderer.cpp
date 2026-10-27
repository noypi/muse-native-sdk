#include "runtime/renderer.h"

namespace runtime {

Renderer::Renderer(DeviceCaps caps) : caps_(caps) {}

DrawError Renderer::draw(const std::vector<DrawCmd>& cmds, Swapchain& target) {
  const BaselineCheck baseline = checkBaseline(caps_);
  if (!baseline.supported) {
    lastError_ = baseline.error;
    return DrawError::UnsupportedBaseline;
  }
  lastError_.clear();
  DrawError result = DrawError::None;
  for (const auto& cmd : cmds) {
    switch (cmd.kind) {
      case PrimitiveKind::Rect:
      case PrimitiveKind::RoundedRect:
      case PrimitiveKind::Image:
      case PrimitiveKind::Text:
        target.submit(cmd);
        break;
      default:
        // RULE-RUNTIME-001: never substitute a default box for an
        // undeclared primitive; report and skip the unknown node.
        result = DrawError::UnknownPrimitive;
        break;
    }
  }
  return result;
}

std::string Renderer::lastError() const { return lastError_; }

}  // namespace runtime
