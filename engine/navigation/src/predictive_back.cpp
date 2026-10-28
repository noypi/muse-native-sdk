#include "navigation/predictive_back.h"

namespace {
constexpr float kCancelStep = 0.05f;

float clamp01(float v) {
  if (!(v >= 0.0f)) {
    // Covers NaN and -inf: NaN fails the comparison, negatives clamp.
    // NaN -> 0 keeps transitionProgress() in [0,1] without throwing.
    return 0.0f;
  }
  if (v > 1.0f) return 1.0f;
  return v;
}
}  // namespace

void PredictiveBack::onProgress(BackProgress p) {
  if (p.cancelled) {
    // Finger-lift cancel (or mid-gesture reversal): decay the driven
    // transition toward 0 regardless of the event's progress value so
    // repeated cancels always floor at exactly 0.
    progress_ = clamp01(progress_ - kCancelStep);
    return;
  }
  progress_ = clamp01(p.progress);
}

float PredictiveBack::transitionProgress() const { return progress_; }
