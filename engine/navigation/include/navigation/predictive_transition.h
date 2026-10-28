#pragma once

#include "navigation/predictive_back.h"

// Gesture-driven predictive transition (FR-NAV-008 / AC-NAV-022). Progress
// frames map 1:1 onto driven progress; `cancelled=true` is the release
// signal: release at progress >= 0.5 commits (latches 1.0), release below
// reverses (resets 0.0 synchronously). No timer replay.
//
// Retarget (AC-NAV-023): `retargetTo(t)` captures the in-flight start
// (`retargetStart_ = driven_`) and the clamped target (`retarget_`), sets
// `hasRetarget_`, and does not move `driven_` itself (no jump). Each
// subsequent non-release `onBackProgress` frame advances `driven_` one step
// toward `retarget_` even when the incoming frame stalls, so `retargetTo`
// owns the reversal; frames that already encode the walk win ties. Release
// clears the retarget state via the commit/reverse path above.
class PredictiveTransition {
 public:
  PredictiveTransition() = default;

  void onBackProgress(const BackProgress& p);
  void retargetTo(float target);
  float drivenProgress() const;
  bool isCommitted() const;
  long long lastStepMicros() const;

 private:
  float driven_ = 0.0f;
  bool committed_ = false;
  float retarget_ = 0.0f;
  float retargetStart_ = 0.0f;
  bool hasRetarget_ = false;
  long long lastStepMicros_ = 0;
};
