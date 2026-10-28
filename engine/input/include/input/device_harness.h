#pragma once

#include <cstdint>
#include <vector>

#include "input/event_model.h"
#include "input/focus_tree.h"

namespace input {

// Phone/tablet only per D1: covers the seven SourceClass enumerants in
// event_model.h (Touch, Stylus, Mouse, Trackpad, Keyboard, Gamepad as the
// phone/tablet controller, Dpad). TV hardware is excluded caller-side: the
// header has no Status/unknown-source representation, so no TV class is ever
// injected here.
std::vector<NodeId> DpadWalkOrder(FocusTree& tree);

// Test-harness sink adapter. Templated on the arena type so hermetic
// in-memory recorders (anything with
// `push(const NormalizedEvent&)`) bind without a virtual arena interface.
// Production use forwards the same calls to the real arena.
template <typename Arena>
class DeviceHarness {
 public:
  explicit DeviceHarness(Arena& arena) : arena_(arena) {}

  void inject(const NormalizedEvent& event) { arena_.push(event); }

  // Six-arg injection through the real NormalizeMotion: one call per
  // SourceClass at the call site (see device_coverage_test.cpp).
  void injectMotion(std::int32_t pointerId, ToolType tool, std::uint32_t buttons,
                    AxisState axes, std::uint32_t modifiers,
                    SourceClass sourceClass) {
    inject(NormalizeMotion(pointerId, tool, buttons, axes, modifiers,
                           sourceClass));
  }

  // D-pad walk over the T2 tree: visits each focusable exactly once, in ring
  // order.
  std::vector<NodeId> dpadTraverse(FocusTree& tree) {
    return DpadWalkOrder(tree);
  }

 private:
  Arena& arena_;
};

}  // namespace input
