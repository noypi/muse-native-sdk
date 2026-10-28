#include "input/haptics.h"

#include <set>
#include <utility>

namespace input {
namespace {

// Default null sink: emissions are dropped when no test probe is installed.
void (*g_sink)(HapticPattern) = nullptr;

// Played (gestureId, outcome) pairs. Suppressed attempts are never recorded
// so re-enabling the gate restores playback.
std::set<std::pair<int, GestureOutcome>> g_played;

void Emit(HapticPattern p) noexcept {
  try {
    if (!ShouldPlayHaptic()) return;
    if (g_sink != nullptr) g_sink(p);
  } catch (...) {
  }
}

}  // namespace

HapticPattern PatternForOutcome(GestureOutcome o) noexcept {
  switch (o) {
    case GestureOutcome::Claim:
      return HapticPattern::Click;
    case GestureOutcome::Drop:
      return HapticPattern::Snap;
    case GestureOutcome::Snap:
      return HapticPattern::Tick;
    case GestureOutcome::Error:
      return HapticPattern::Error;
    case GestureOutcome::LongPress:
      return HapticPattern::LongPress;
  }
  return HapticPattern::Click;
}

void Play(HapticPattern p) noexcept { Emit(p); }

void AttachToGesture(int gestureId, GestureOutcome outcome) noexcept {
  try {
    if (!ShouldPlayHaptic()) return;
    auto key = std::make_pair(gestureId, outcome);
    if (g_played.find(key) != g_played.end()) return;
    g_played.insert(key);
    Emit(PatternForOutcome(outcome));
  } catch (...) {
  }
}

void SetHapticTestSink(void (*sink)(HapticPattern)) noexcept {
  g_sink = sink;
}

}  // namespace input
