#pragma once

// Predictive-back NDK progress stream (FR-NAV-005 / AC-NAV-010). Consumes
// the input back-swipe BackProgress struct; each onProgress updates the
// driven transition synchronously.
struct BackProgress {
  float progress = 0.0f;
  int edge = 0;
  bool cancelled = false;
};

class PredictiveBack {
 public:
  PredictiveBack() = default;

  void onProgress(BackProgress p);
  float transitionProgress() const;

 private:
  float progress_ = 0.0f;
};
