#pragma once

#include <cstdint>

namespace input {

enum class ToolType {
  Touch,
  Stylus,
  Mouse,
  Trackpad,
  Key,
  Gamepad,
  Dpad,
};

enum class SourceClass {
  Touch,
  Stylus,
  Mouse,
  Trackpad,
  Keyboard,
  Gamepad,
  Dpad,
};

struct AxisState {
  float x = 0.0f;
  float y = 0.0f;
  float pressure = 0.0f;
};

struct NormalizedEvent {
  std::int32_t pointerId = 0;
  ToolType tool = ToolType::Touch;
  std::uint32_t buttons = 0u;
  AxisState axes{};
  std::uint32_t modifiers = 0u;
  SourceClass sourceClass = SourceClass::Touch;
};

NormalizedEvent NormalizeMotion(std::int32_t pointerId, ToolType tool,
                                std::uint32_t buttons, AxisState axes,
                                std::uint32_t modifiers,
                                SourceClass sourceClass);

}  // namespace input
