// T8 contract tests — FR-NAV-008 / AC-NAV-023 (TC-NAV-023).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
//
// Binds to the plan T8 `Interface:` line (verbatim):
//   `void retargetTo(float target); float drivenProgress() const;`
//   `long long lastStepMicros() const;`
//
// ASSUMPTION (class): the plan names unqualified member functions; tests call
// them on the existing `PredictiveTransition` class in the global namespace
// (extended by the coder with the retarget path — T8 depends on T6/T7).
// ASSUMPTION (retarget semantics): `retargetTo(t)` re-targets the in-flight
// transition from the current progress value toward clamped `t` in [0,1]
// without restarting from 0/1; the walk-back is re-driven through
// `onBackProgress` frames (the reversed-gesture stream), each step advancing
// `drivenProgress()` toward the target. The retarget call itself must not
// jump: `|drivenProgress() after retarget − before| <= 0.05`.
// ASSUMPTION (budget): every step (the retarget call and each subsequent
// frame) completes within the NFR-NAV-003 4ms transition budget, i.e.
// `lastStepMicros() <= 4000` after each step.
// Shared-element flash / finger-tracked preview (TC-NAV-024) is device-lab
// scope and is explicitly NOT asserted here (plan Detail).
#include <cmath>
#include <cstdio>

#include "navigation/predictive_back.h"
#include "navigation/predictive_transition.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

bool approx(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) < eps;
}

BackProgress frame(float progress) {
  BackProgress p;
  p.progress = progress;
  p.edge = 0;
  p.cancelled = false;
  return p;
}

constexpr long long kBudgetMicros = 4000;

bool check_drive_to_point_six() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.2f));
  t.onBackProgress(frame(0.6f));
  return approx(t.drivenProgress(), 0.6f);
}

bool check_retarget_call_itself_does_not_jump() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.6f));
  float before = t.drivenProgress();
  t.retargetTo(0.0f);
  float after = t.drivenProgress();
  return std::fabs(after - before) <= 0.05f;
}

bool check_walk_back_is_continuous_no_jump_over_005() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.6f));
  t.retargetTo(0.0f);
  float prev = t.drivenProgress();
  const float steps[] = {0.55f, 0.5f, 0.45f, 0.4f, 0.35f, 0.3f,
                         0.25f, 0.2f, 0.15f, 0.1f, 0.05f, 0.0f};
  for (float s : steps) {
    t.onBackProgress(frame(s));
    float cur = t.drivenProgress();
    if (std::fabs(cur - prev) > 0.05f + 1e-5f) return false;
    if (cur > prev + 1e-5f) return false;  // must walk back, not forward
    prev = cur;
  }
  return approx(t.drivenProgress(), 0.0f);
}

bool check_every_step_within_budget() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.6f));
  if (t.lastStepMicros() > kBudgetMicros) return false;
  t.retargetTo(0.0f);
  if (t.lastStepMicros() > kBudgetMicros) return false;
  const float steps[] = {0.5f, 0.4f, 0.3f, 0.2f, 0.1f, 0.0f};
  for (float s : steps) {
    t.onBackProgress(frame(s));
    if (t.lastStepMicros() > kBudgetMicros) return false;
  }
  return true;
}

bool check_retarget_clamps_out_of_range_target() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.6f));
  t.retargetTo(-2.0f);
  if (t.drivenProgress() < -1e-5f || t.drivenProgress() > 1.0f + 1e-5f)
    return false;
  t.retargetTo(5.0f);
  if (t.drivenProgress() < -1e-5f || t.drivenProgress() > 1.0f + 1e-5f)
    return false;
  return true;
}

bool check_retarget_mid_gesture_does_not_restart_from_one() {
  // Reversal from 0.6 toward 0 must never snap to 1.0 (no restart).
  PredictiveTransition t;
  t.onBackProgress(frame(0.6f));
  t.retargetTo(0.0f);
  t.onBackProgress(frame(0.55f));
  return t.drivenProgress() < 0.65f && t.drivenProgress() > 0.45f;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_drive_to_point_six(), "drive_to_point_six");
  EXPECT_TRUE(check_retarget_call_itself_does_not_jump(),
              "retarget_call_itself_does_not_jump");
  EXPECT_TRUE(check_walk_back_is_continuous_no_jump_over_005(),
              "walk_back_is_continuous_no_jump_over_005");
  EXPECT_TRUE(check_every_step_within_budget(), "every_step_within_budget");
  EXPECT_TRUE(check_retarget_clamps_out_of_range_target(),
              "retarget_clamps_out_of_range_target");
  EXPECT_TRUE(check_retarget_mid_gesture_does_not_restart_from_one(),
              "retarget_mid_gesture_does_not_restart_from_one");
  if (failures == 0) {
    std::printf("ALL PASS\n");
  }
  return failures == 0 ? 0 : 1;
}
