#include "layout/fold_signal.h"

namespace layout {

void FoldSignal::update(const FoldingFeatureState& s) noexcept {
  state_ = s.state;
  orientation_ = s.orientation;
  occlusion_ = s.occlusion;
  ++revision_;
}

FoldState FoldSignal::state() const noexcept { return state_; }

FoldOrientation FoldSignal::orientation() const noexcept { return orientation_; }

OcclusionType FoldSignal::occlusion() const noexcept { return occlusion_; }

std::uint64_t FoldSignal::revision() const noexcept { return revision_; }

}  // namespace layout
