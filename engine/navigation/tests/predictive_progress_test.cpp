// T7 contract tests — FR-NAV-008 / AC-NAV-022 (TC-NAV-022).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
//
// Binds to the plan T7 `Interface:` line (verbatim):
//   `void onBackProgress(const BackProgress& p); float drivenProgress() const;`
//   `bool isCommitted() const;`
//   (commit when progress ≥ 0.5 on release, reverse otherwise)
// where `BackProgress{progress,edge,cancelled}` is the INPUT gesture-stream
// shape consumed from `navigation/predictive_back.h` (required reuse
// contract — no new input code).
//
// ASSUMPTION (class): the plan names unqualified member functions; tests call
// them on a `PredictiveTransition` class in the global namespace holding one
// in-flight transition. If the coder names the class or namespace
// differently, update the call sites — not the expected behavior.
// ASSUMPTION (release signal): `BackProgress` carries no explicit release
// event, so `cancelled=true` is treated as the gesture-release/end signal at
// which the commit/reverse decision is taken: release with last progress
// ≥ 0.5 commits, release below reverses toward 0. Mid-gesture frames use
// `cancelled=false` and map 1:1 onto driven progress.
// ASSUMPTION (post-decision values): a commit latches `drivenProgress()` to
// 1.0f and `isCommitted()` to true; a reverse resets `drivenProgress()` to
// 0.0f synchronously and leaves `isCommitted()` false.
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

bool approx(float a, float b) { return std::fabs(a - b) < 1e-5f; }

BackProgress frame(float progress) {
  BackProgress p;
  p.progress = progress;
  p.edge = 0;
  p.cancelled = false;
  return p;
}

BackProgress release(float progress) {
  BackProgress p;
  p.progress = progress;
  p.edge = 0;
  p.cancelled = true;
  return p;
}

bool check_initial_state_is_uncommitted_at_zero() {
  PredictiveTransition t;
  return approx(t.drivenProgress(), 0.0f) && !t.isCommitted();
}

bool check_progress_maps_one_to_one_before_release() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.0f));
  if (!approx(t.drivenProgress(), 0.0f) || t.isCommitted()) return false;
  t.onBackProgress(frame(0.3f));
  if (!approx(t.drivenProgress(), 0.3f) || t.isCommitted()) return false;
  t.onBackProgress(frame(0.7f));
  return approx(t.drivenProgress(), 0.7f) && !t.isCommitted();
}

bool check_release_at_or_above_half_commits() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.0f));
  t.onBackProgress(frame(0.7f));
  if (!approx(t.drivenProgress(), 0.7f)) return false;
  t.onBackProgress(release(0.7f));
  return t.isCommitted() && approx(t.drivenProgress(), 1.0f);
}

bool check_release_exactly_at_half_commits() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.5f));
  t.onBackProgress(release(0.5f));
  return t.isCommitted();
}

bool check_release_below_half_reverses() {
  PredictiveTransition t;
  t.onBackProgress(frame(0.4f));
  if (!approx(t.drivenProgress(), 0.4f)) return false;
  t.onBackProgress(release(0.4f));
  return !t.isCommitted() && approx(t.drivenProgress(), 0.0f);
}

bool check_release_from_zero_stays_uncommitted() {
  PredictiveTransition t;
  t.onBackProgress(release(0.0f));
  return !t.isCommitted() && approx(t.drivenProgress(), 0.0f);
}

bool check_cancel_flag_does_not_commit_mid_gesture_value() {
  // A low-progress gesture must never commit, even though frames arrived.
  PredictiveTransition t;
  t.onBackProgress(frame(0.1f));
  t.onBackProgress(frame(0.2f));
  t.onBackProgress(release(0.2f));
  return !t.isCommitted() && approx(t.drivenProgress(), 0.0f);
}

}  // namespace

int main() {
  EXPECT_TRUE(check_initial_state_is_uncommitted_at_zero(),
              "initial_state_is_uncommitted_at_zero");
  EXPECT_TRUE(check_progress_maps_one_to_one_before_release(),
              "progress_maps_one_to_one_before_release");
  EXPECT_TRUE(check_release_at_or_above_half_commits(),
              "release_at_or_above_half_commits");
  EXPECT_TRUE(check_release_exactly_at_half_commits(),
              "release_exactly_at_half_commits_boundary");
  EXPECT_TRUE(check_release_below_half_reverses(),
              "release_below_half_reverses");
  EXPECT_TRUE(check_release_from_zero_stays_uncommitted(),
              "release_from_zero_stays_uncommitted");
  EXPECT_TRUE(check_cancel_flag_does_not_commit_mid_gesture_value(),
              "low_progress_release_never_commits");
  if (failures == 0) {
    std::printf("ALL PASS\n");
  }
  return failures == 0 ? 0 : 1;
}
