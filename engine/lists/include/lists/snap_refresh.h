#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lists {

// Snap-point set: candidate resting offsets in pixels.
struct SnapPoints {
  std::vector<float> points;
};

// Pull-to-refresh overscroll state machine: Idle below threshold, Armed
// past threshold, Refreshing on release-past-threshold (modelled as
// pullRefresh(Armed, overshoot)).
enum class RefreshState { Idle, Armed, Refreshing };

// Scrollable axes of a nested scroller (RULE-LISTS-001 interop).
struct NestedAxis {
  bool scrollsHorizontal = false;
  bool scrollsVertical = false;
};

// Which scroller claims the gesture.
enum class NestedDecision { Parent, Child };

// Benchmark report for benchDiff: echoes the inputs, carries the measured
// C++ time plus a documented baseline estimate, and states the claimed
// speedup as a band (never a bare ratio).
struct PerfReport {
  size_t n = 0;
  size_t edits = 0;
  double secondsCpp = 0.0;
  double secondsBaseline = 0.0;
  double speedupLow = 0.0;
  double speedupHigh = 0.0;
  std::string methodology;
};

// Settles `offsetPx` onto a snap point. At rest (~zero velocity) the
// nearest point wins (midpoint ties go to the lower point); a strong fling
// steps one point in the fling direction from the nearest point when one
// exists (midpoint ties resolve toward the fling). An empty set returns
// `offsetPx` unchanged.
float snapTarget(float offsetPx, float velocityPxS, const SnapPoints& pts);

// Pull-to-refresh overscroll state machine. Zero/negative overscroll never
// triggers: any state with `overscrollPx <= 0` returns Idle. Otherwise Idle
// past the threshold arms, Armed past the threshold refreshes (release
// modelled as a second overshooting call), Armed below the threshold
// disarms, and Refreshing sticks.
RefreshState pullRefresh(RefreshState s, float overscrollPx);

// Nested-scroll handoff (RULE-LISTS-001): when parent and child share a
// scrollable axis the parent claims the gesture; when the child scrolls on
// an axis the parent does not, the child keeps its own axis.
NestedDecision nestedClaim(NestedAxis parent, NestedAxis child);

// Small-N diff benchmark harness over `lists::myersDiff` (reused, never
// reimplemented). Builds a synthetic `n`-key feed, applies `edits`
// single-key substitutions, times the C++ diff, and reports a documented
// speedup band with a large-feed scaling note. Kept fast by contract: the
// suite calls benchDiff(1000, 10), never a real 10k timing loop.
PerfReport benchDiff(size_t n, size_t edits);

}  // namespace lists
