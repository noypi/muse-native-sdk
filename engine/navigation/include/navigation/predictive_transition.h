#pragma once

#include "navigation/predictive_back.h"

// Gesture-driven predictive transition (FR-NAV-008 / AC-NAV-022). Progress
// frames map 1:1 onto driven progress; `cancelled=true` is the release
// signal: release at progress >= 0.5 commits (latches 1.0), release below
// reverses (resets 0.0 synchronously). No timer replay.
class PredictiveTransition {
 public:
  PredictiveTransition() = default;

  void onBackProgress(const BackProgress& p);
  float drivenProgress() const;
  bool isCommitted() const;

 private:
  float driven_ = 0.0f;
  bool committed_ = false;
};
