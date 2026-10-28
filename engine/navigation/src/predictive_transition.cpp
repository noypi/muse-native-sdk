#include "navigation/predictive_transition.h"

#include <chrono>
#include <cmath>

namespace {
constexpr float kCommitThreshold = 0.5f;
constexpr float kRetargetStep = 0.05f;

float clamp01(float v) {
  if (!(v >= 0.0f)) {
    return 0.0f;
  }
  if (v > 1.0f) return 1.0f;
  return v;
}
}  // namespace

void PredictiveTransition::onBackProgress(const BackProgress& p) {
  const auto start = std::chrono::steady_clock::now();
  if (p.cancelled) {
    // Release signal: commit at >= 0.5, reverse otherwise.
    if (clamp01(p.progress) >= kCommitThreshold) {
      driven_ = 1.0f;
      committed_ = true;
    } else {
      driven_ = 0.0f;
      committed_ = false;
    }
    hasRetarget_ = false;
  } else if (hasRetarget_) {
    // Retarget-owned reversal: advance driven_ one bounded step from the
    // stored start toward the stored target even when the frame stalls.
    // A frame that already encodes the walk (closer to the target) wins.
    const float f = clamp01(p.progress);
    float stepped = driven_;
    if (driven_ < retarget_) {
      stepped = std::fmin(driven_ + kRetargetStep, retarget_);
    } else if (driven_ > retarget_) {
      stepped = std::fmax(driven_ - kRetargetStep, retarget_);
    }
    if (std::fabs(f - retarget_) <= std::fabs(stepped - retarget_)) {
      driven_ = f;
    } else {
      driven_ = stepped;
    }
    if (driven_ == retarget_) hasRetarget_ = false;
    committed_ = false;
  } else {
    driven_ = clamp01(p.progress);
    committed_ = false;
  }
  const auto end = std::chrono::steady_clock::now();
  lastStepMicros_ =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();
}

void PredictiveTransition::retargetTo(float target) {
  const auto start = std::chrono::steady_clock::now();
  // Re-target from the current driven value toward the clamped target.
  // No restart from 0/1 and no jump: driven_ is untouched here; the stored
  // start/target pair is consumed by subsequent onBackProgress frames, which
  // step driven_ toward retarget_ (see above).
  retargetStart_ = driven_;
  retarget_ = clamp01(target);
  hasRetarget_ = true;
  committed_ = false;
  const auto end = std::chrono::steady_clock::now();
  lastStepMicros_ =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();
}

float PredictiveTransition::drivenProgress() const { return driven_; }

bool PredictiveTransition::isCommitted() const { return committed_; }

long long PredictiveTransition::lastStepMicros() const {
  return lastStepMicros_;
}
