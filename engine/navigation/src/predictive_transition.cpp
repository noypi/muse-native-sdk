#include "navigation/predictive_transition.h"

namespace {
constexpr float kCommitThreshold = 0.5f;

float clamp01(float v) {
  if (!(v >= 0.0f)) {
    return 0.0f;
  }
  if (v > 1.0f) return 1.0f;
  return v;
}
}  // namespace

void PredictiveTransition::onBackProgress(const BackProgress& p) {
  if (p.cancelled) {
    // Release signal: commit at >= 0.5, reverse otherwise.
    if (clamp01(p.progress) >= kCommitThreshold) {
      driven_ = 1.0f;
      committed_ = true;
    } else {
      driven_ = 0.0f;
      committed_ = false;
    }
    return;
  }
  driven_ = clamp01(p.progress);
  committed_ = false;
}

float PredictiveTransition::drivenProgress() const { return driven_; }

bool PredictiveTransition::isCommitted() const { return committed_; }
