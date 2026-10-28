#include "input/event_model.h"

namespace input {

NormalizedEvent NormalizeMotion(std::int32_t pointerId, ToolType tool,
                                std::uint32_t buttons, AxisState axes,
                                std::uint32_t modifiers,
                                SourceClass sourceClass) {
  NormalizedEvent out{};
  out.pointerId = pointerId;
  out.tool = tool;
  out.buttons = buttons;
  out.axes = axes;
  out.modifiers = modifiers;
  out.sourceClass = sourceClass;
  return out;
}

}  // namespace input
