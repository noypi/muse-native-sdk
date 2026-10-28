#pragma once

#include <cstdint>

#include "layout/form_factor.h"

namespace layout {

// Thin layout-consumable signal over T1's `FoldingFeatureState`, refreshed
// from the layout-change callback path. Subscribers poll `revision()`:
// every `update()` publishes the latest state/orientation/occlusion
// (last-write-wins) and bumps the revision by exactly one, even for
// byte-identical input. Never throws.
class FoldSignal {
 public:
  FoldSignal() = default;

  void update(const FoldingFeatureState& s) noexcept;
  FoldState state() const noexcept;
  FoldOrientation orientation() const noexcept;
  OcclusionType occlusion() const noexcept;
  std::uint64_t revision() const noexcept;

 private:
  FoldState state_ = FoldState::Unknown;
  FoldOrientation orientation_ = FoldOrientation::Vertical;
  OcclusionType occlusion_ = OcclusionType::None;
  std::uint64_t revision_ = 0;
};

}  // namespace layout
