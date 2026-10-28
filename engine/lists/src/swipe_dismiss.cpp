#include "lists/swipe_dismiss.h"

#include <algorithm>

namespace lists {

SwipeOutcome UpdateSwipe(SwipeState& s, float dx, float velocity) {
  const float threshold = s.thresholdPx > 0.0f ? s.thresholdPx : 1.0f;
  // Commit on distance OR fling velocity.
  if (dx >= threshold || velocity >= s.velocityThreshold) {
    s.active = false;
    return SwipeOutcome{SwipePhase::Committed, 1.0f};
  }
  // Release of an in-flight partial swipe back to rest cancels cleanly:
  // transform reset, no visual residue.
  if (dx <= 0.0f) {
    if (s.active) {
      s.active = false;
      return SwipeOutcome{SwipePhase::Cancelled, 0.0f};
    }
    return SwipeOutcome{SwipePhase::Tracking, 0.0f};
  }
  // Partial swipe: sublinear resistance so progress trails dx/threshold.
  s.active = true;
  const float frac = std::min(dx / threshold, 1.0f);
  const float progress = frac / (1.0f + frac);
  return SwipeOutcome{SwipePhase::Tracking, progress};
}

bool UndoDismiss(PendingDismiss& d, ListModel& m) {
  if (!d.armed) {
    return false;
  }
  const size_t at = std::min(d.index, m.order.size());
  m.order.insert(m.order.begin() + static_cast<std::ptrdiff_t>(at), d.key);
  d.armed = false;
  return true;
}

}  // namespace lists
