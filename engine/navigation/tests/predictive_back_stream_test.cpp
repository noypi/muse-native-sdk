// T3 contract tests — FR-NAV-005 / AC-NAV-010 (TC-NAV-010).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T3 `Interface:` line verbatim (see unit-tests/T3.md).
//
// ASSUMPTION (construction): the plan `Interface:` line names no
// constructor. These cases use a default-constructible `PredictiveBack`.
// If the coder requires route/home construction, that is a
// reconciliation (update call sites, not expectations).
// ASSUMPTION (cancel semantics): `cancelled==true` drives the reported
// progress back toward 0; repeated cancelled events reach 0. The exact
// decay step is coder-owned; these cases assert direction (strictly
// decreasing, floors at 0), not the step size.
#include <chrono>
#include <cstdio>

#include "navigation/predictive_back.h"

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

bool check_initial_progress_is_zero() {
  PredictiveBack b;
  return b.transitionProgress() == 0.0f;
}

bool check_progress_tracks_event_value() {
  PredictiveBack b;
  b.onProgress(BackProgress{0.4f, 0, false});
  float v = b.transitionProgress();
  return v >= 0.39f && v <= 0.41f;
}

bool check_monotonic_sixty_event_stream() {
  PredictiveBack b;
  float prev = 0.0f;
  for (int i = 1; i <= 60; ++i) {
    b.onProgress(BackProgress{static_cast<float>(i) / 60.0f, 0, false});
    float v = b.transitionProgress();
    if (v + 1e-6f < prev) return false;
    prev = v;
  }
  return prev >= 0.99f && prev <= 1.0f;
}

bool check_progress_clamped_above_one() {
  PredictiveBack b;
  b.onProgress(BackProgress{1.5f, 0, false});
  return b.transitionProgress() == 1.0f;
}

bool check_progress_clamped_below_zero() {
  PredictiveBack b;
  b.onProgress(BackProgress{0.5f, 0, false});
  b.onProgress(BackProgress{-0.3f, 0, false});
  return b.transitionProgress() == 0.0f;
}

bool check_cancel_reverses_toward_zero() {
  PredictiveBack b;
  b.onProgress(BackProgress{0.8f, 0, false});
  float before = b.transitionProgress();
  if (before <= 0.0f) return false;
  b.onProgress(BackProgress{0.8f, 0, true});
  float after = b.transitionProgress();
  if (!(after < before)) return false;
  for (int i = 0; i < 64; ++i) {
    b.onProgress(BackProgress{0.0f, 0, true});
  }
  return b.transitionProgress() == 0.0f;
}

bool check_commit_at_lift_reports_full_progress() {
  PredictiveBack b;
  for (int i = 1; i <= 60; ++i) {
    b.onProgress(BackProgress{static_cast<float>(i) / 60.0f, 0, false});
  }
  float v = b.transitionProgress();
  return v >= 0.99f && v <= 1.0f;
}

bool check_per_event_handling_under_1ms() {
  PredictiveBack b;
  auto start = std::chrono::steady_clock::now();
  for (int i = 0; i < 60; ++i) {
    b.onProgress(BackProgress{static_cast<float>(i) / 60.0f, 0, false});
    volatile float sink = b.transitionProgress();
    (void)sink;
  }
  auto end = std::chrono::steady_clock::now();
  auto micros =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();
  // 60 events in <60ms  <=>  <1ms per event on average (host-side proxy).
  return micros < 60000;
}

bool check_no_throw_on_extreme_input() {
  PredictiveBack b;
  try {
    b.onProgress(BackProgress{1e30f, -1, false});
    b.onProgress(BackProgress{-1e30f, -1, true});
    volatile float sink = b.transitionProgress();
    (void)sink;
  } catch (...) {
    return false;
  }
  float v = b.transitionProgress();
  return v >= 0.0f && v <= 1.0f;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_initial_progress_is_zero(), "initial_progress_is_zero");
  EXPECT_TRUE(check_progress_tracks_event_value(),
              "progress_tracks_event_value");
  EXPECT_TRUE(check_monotonic_sixty_event_stream(),
              "monotonic_sixty_event_stream");
  EXPECT_TRUE(check_progress_clamped_above_one(),
              "progress_clamped_above_one");
  EXPECT_TRUE(check_progress_clamped_below_zero(),
              "progress_clamped_below_zero");
  EXPECT_TRUE(check_cancel_reverses_toward_zero(),
              "cancel_reverses_toward_zero");
  EXPECT_TRUE(check_commit_at_lift_reports_full_progress(),
              "commit_at_lift_reports_full_progress");
  EXPECT_TRUE(check_per_event_handling_under_1ms(),
              "per_event_handling_under_1ms");
  EXPECT_TRUE(check_no_throw_on_extreme_input(),
              "no_throw_on_extreme_input");
  if (failures > 0) {
    std::printf("predictive_back_stream_test: %d failing\n", failures);
    return 1;
  }
  std::printf("predictive_back_stream_test: all passing\n");
  return 0;
}
