#include "navigation/predictive_transition.h"

#include <chrono>

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
  // No restart from 0/1 and no jump: driven_ is untouched here; the
  // walk-back is re-driven through subsequent onBackProgress frames.
  retarget_ = clamp01(target);
  hasRetarget_ = true;
  committed_ = false;
  (void)retarget_;
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
