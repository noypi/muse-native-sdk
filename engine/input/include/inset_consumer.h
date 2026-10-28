#pragma once

namespace input {

// Read-only view of the typed insets surface (layout-owned LAYOUT-INSETS,
// served by the state-owned Inset Vector Bus). Plain trivially-copyable data:
// input retains values only, never a bus handle.
struct TypedInsetsView {
  float contentW = 0.0f;
  float contentH = 0.0f;
  float systemBarTop = 0.0f;
  float systemBarBottom = 0.0f;
  float systemBarLeft = 0.0f;
  float systemBarRight = 0.0f;
};

// Consumption-only view of the inset bus (plan A3): reads the view, owns
// nothing, registers no writer and performs no bus writes.
class InsetConsumer {
 public:
  InsetConsumer() = default;

  void ApplyInsets(const TypedInsetsView& insets) { view_ = insets; }

  bool TouchBlocked(float x, float y) const {
    if (y < view_.systemBarTop) return true;
    if (y > view_.contentH - view_.systemBarBottom) return true;
    if (x < view_.systemBarLeft) return true;
    if (x > view_.contentW - view_.systemBarRight) return true;
    return false;
  }

 private:
  TypedInsetsView view_{};
};

}  // namespace input
