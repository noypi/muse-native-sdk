#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lists {

// Phase of an in-flight horizontal swipe gesture.
enum class SwipePhase { Tracking, Committed, Cancelled };

// Outcome of one swipe update: the phase plus normalized progress in [0, 1].
// Tracking progress is sublinear in dx/threshold (resistance); a commit
// reports 1.0; a cancel resets to 0.0 (no visual residue).
struct SwipeOutcome {
  SwipePhase state = SwipePhase::Tracking;
  float progress = 0.0f;
};

// Input thresholds plus opaque per-gesture bookkeeping. `thresholdPx` is the
// distance commit line, `velocityThreshold` the fling commit line; commit
// fires when dx >= thresholdPx OR velocity >= velocityThreshold. `active`
// tracks whether a partial swipe is in flight so a release back to zero
// cancels rather than idles.
struct SwipeState {
  float thresholdPx = 120.0f;
  float velocityThreshold = 800.0f;
  bool active = false;
};

// A committed dismissal awaiting the undo window. `armed` models the
// cancellable window: commit arms it, lapse/confirm disarms it, and each
// undo is single-shot (disarms on success).
struct PendingDismiss {
  std::string key;
  size_t index = 0;
  bool armed = false;
};

// Minimal list model for undo: the current key order. A committed dismiss
// has already removed `PendingDismiss::key`; undo re-inserts it.
struct ListModel {
  std::vector<std::string> order;
};

// Advances the swipe by (`dx`, `velocity`). Below both thresholds the
// gesture keeps tracking with sublinear resistance progress; past either
// threshold it commits at full progress; releasing an in-flight partial
// swipe back to zero cancels with progress reset. Never throws.
SwipeOutcome UpdateSwipe(SwipeState& s, float dx, float velocity);

// Restores `d.key` at `d.index` when `d.armed` (the undo window is open),
// disarms `d`, and returns true. A disarmed or already-consumed dismiss
// returns false and touches neither `d` nor `m`. Never throws.
bool UndoDismiss(PendingDismiss& d, ListModel& m);

}  // namespace lists
