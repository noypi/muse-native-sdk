// T3 contract tests — FR-LISTS-004 / AC-LISTS-020 (TC-LISTS-020).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T3 `Interface:` lines:
//   `SwipeOutcome UpdateSwipe(SwipeState& s, float dx, float velocity);`
//   `bool UndoDismiss(PendingDismiss& d, ListModel& m);`
//   (restores the item at its original index; returns true on restore).
// Constrained by D1 (horizontal swipe claims go to the arena/parent):
// this signature carries only the horizontal `dx`, so direction/arena
// filtering lives outside it — D1 is covered by the T5 manual procedure,
// noted in contract T3.md, not asserted here.
// Include path follows the engine convention:
//   #include "lists/swipe_dismiss.h" with engine/lists/include on the path.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or any struct shapes. This test proposes: namespace
// `lists`; `SwipePhase::{Tracking, Committed, Cancelled}`;
// `SwipeOutcome{SwipePhase state; float progress}` with progress in
// [0, 1]; `SwipeState{float thresholdPx; float velocityThreshold;
// /* + opaque progress/commit bookkeeping */}`; `PendingDismiss{
// std::string key; size_t index; bool armed; }`; `ListModel{
// std::vector<std::string> order; }`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (thresholds, resistance, clean
// cancel, undo restores the original index).
//
// ASSUMPTION (thresholds): dismissal commits when `dx >= thresholdPx` OR
// `velocity >= velocityThreshold` with partial progress; release below both
// cancels with progress reset to 0 (no visual residue). Exact constant
// values are the coder's; only the relations are asserted.
//
// ASSUMPTION (resistance): partial-swipe progress is sublinear in
// `dx / thresholdPx` (resistance), i.e. progress < dx/threshold for
// 0 < dx < threshold.
//
// ASSUMPTION (undo window): `PendingDismiss::armed` models the cancellable
// undo window — commit arms it, lapse/confirm disarms. Undo of a disarmed
// (or already-consumed) dismiss returns false and touches nothing.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/swipe_dismiss.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::SwipeState fresh_state() {
  lists::SwipeState s{};
  s.thresholdPx = 120.0f;
  s.velocityThreshold = 800.0f;
  return s;
}

lists::ListModel model_n(size_t n) {
  lists::ListModel m{};
  for (size_t i = 0; i < n; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    m.order.emplace_back(buf);
  }
  return m;
}

// Below both thresholds: still tracking, partial progress.
void case_sub_threshold_tracks() {
  lists::SwipeState s = fresh_state();
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/40.0f,
                                             /*velocity=*/100.0f);
  check(o.state == lists::SwipePhase::Tracking, "sub_threshold_tracks");
  check(o.progress > 0.0f && o.progress < 1.0f,
        "sub_threshold_partial_progress");
}

// Past the distance threshold: committed.
void case_past_threshold_commits() {
  lists::SwipeState s = fresh_state();
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/150.0f,
                                             /*velocity=*/100.0f);
  check(o.state == lists::SwipePhase::Committed, "past_threshold_commits");
  check(o.progress >= 1.0f, "committed_full_progress");
}

// Fast fling past the velocity threshold commits even mid-distance.
void case_fling_velocity_commits() {
  lists::SwipeState s = fresh_state();
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/60.0f,
                                             /*velocity=*/1200.0f);
  check(o.state == lists::SwipePhase::Committed, "fling_velocity_commits");
}

// Slow release below threshold cancels cleanly: progress reset, no residue.
void case_slow_release_cancels_clean() {
  lists::SwipeState s = fresh_state();
  lists::UpdateSwipe(s, /*dx=*/40.0f, /*velocity=*/100.0f);
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/0.0f,
                                             /*velocity=*/0.0f);
  check(o.state == lists::SwipePhase::Cancelled, "slow_release_cancels");
  check(o.progress == 0.0f, "cancel_resets_transform");
}

// Partial-swipe resistance: progress trails the linear fraction.
void case_partial_resistance() {
  lists::SwipeState s = fresh_state();
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/60.0f,
                                             /*velocity=*/0.0f);
  check(o.state == lists::SwipePhase::Tracking, "partial_still_tracks");
  check(o.progress < 60.0f / 120.0f, "partial_resists");
}

// Boundary: zero displacement is tracked at zero progress.
void case_zero_dx() {
  lists::SwipeState s = fresh_state();
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/0.0f,
                                             /*velocity=*/0.0f);
  check(o.state == lists::SwipePhase::Tracking && o.progress == 0.0f,
        "zero_dx_zero_progress");
}

// Boundary: non-positive threshold (0) with zero displacement must not
// commit (clamped threshold line); expect Tracking or Cancelled.
void case_nonpositive_threshold_no_commit() {
  lists::SwipeState s = fresh_state();
  s.thresholdPx = 0.0f;
  lists::SwipeOutcome o = lists::UpdateSwipe(s, /*dx=*/0.0f,
                                             /*velocity=*/0.0f);
  check(o.state == lists::SwipePhase::Tracking ||
            o.state == lists::SwipePhase::Cancelled,
        "zero_threshold_no_commit");
}

// TC-LISTS-020 core: undo restores the item at its original index.
void case_undo_restores_index() {
  lists::ListModel m = model_n(5);
  // Dismiss k00002 (index 2): model no longer holds it.
  m.order.erase(m.order.begin() + 2);
  lists::PendingDismiss d{};
  d.key = "k00002";
  d.index = 2;
  d.armed = true;
  check(lists::UndoDismiss(d, m), "undo_returns_true");
  std::vector<std::string> want = {"k00000", "k00001", "k00002", "k00003",
                                   "k00004"};
  check(m.order == want, "undo_restores_original_index");
}

// Undo outside the window (disarmed) fails and touches nothing.
void case_undo_disarmed_fails() {
  lists::ListModel m = model_n(5);
  m.order.erase(m.order.begin() + 2);
  std::vector<std::string> before = m.order;
  lists::PendingDismiss d{};
  d.key = "k00002";
  d.index = 2;
  d.armed = false;
  check(!lists::UndoDismiss(d, m), "undo_disarmed_fails");
  check(m.order == before, "undo_disarmed_no_touch");
}

// Undo is single-shot: a second undo consumes nothing.
void case_undo_single_shot() {
  lists::ListModel m = model_n(4);
  m.order.erase(m.order.begin() + 1);
  lists::PendingDismiss d{};
  d.key = "k00001";
  d.index = 1;
  d.armed = true;
  check(lists::UndoDismiss(d, m), "undo_first_succeeds");
  size_t size_after = m.order.size();
  check(!lists::UndoDismiss(d, m), "undo_second_fails");
  check(m.order.size() == size_after, "undo_second_no_duplicate");
}

// Boundary: undo restores the last item at the end.
void case_undo_last_item() {
  lists::ListModel m = model_n(3);
  m.order.pop_back();
  lists::PendingDismiss d{};
  d.key = "k00002";
  d.index = 2;
  d.armed = true;
  check(lists::UndoDismiss(d, m), "undo_last_returns_true");
  check(m.order.size() == 3 && m.order.back() == "k00002",
        "undo_last_restores_end");
}

}  // namespace

int main() {
  case_sub_threshold_tracks();
  case_past_threshold_commits();
  case_fling_velocity_commits();
  case_slow_release_cancels_clean();
  case_partial_resistance();
  case_zero_dx();
  case_nonpositive_threshold_no_commit();
  case_undo_restores_index();
  case_undo_disarmed_fails();
  case_undo_single_shot();
  case_undo_last_item();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
