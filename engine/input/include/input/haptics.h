#pragma once

#include "input/haptics_settings.h"

namespace input {

enum class HapticPattern {
  Click,
  Tick,
  Snap,
  Error,
  LongPress,
};

enum class GestureOutcome {
  Claim,
  Drop,
  Snap,
  Error,
  LongPress,
};

void Play(HapticPattern p) noexcept;
void AttachToGesture(int gestureId, GestureOutcome outcome) noexcept;
HapticPattern PatternForOutcome(GestureOutcome o) noexcept;

// Test-only emission probe (ASSUMPTION T3-SINK): records emissions
// in-process instead of calling the platform vibrator. nullptr restores
// the default null sink (emissions are dropped).
void SetHapticTestSink(void (*sink)(HapticPattern)) noexcept;

}  // namespace input
