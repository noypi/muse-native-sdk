#include "motion/clock.h"

#include <chrono>
#include <limits>

namespace {

VirtualClock* g_clock = nullptr;
bool g_clockError = false;

}  // namespace

void VirtualClock::advanceFrame(int64_t dtNanos) noexcept {
  if (dtNanos <= 0) {
    g_clockError = true;
    return;
  }
  // Saturate instead of overflowing (undefined behavior) on absurd inputs;
  // normal frame deltas accumulate exactly.
  if (nanos_ > (std::numeric_limits<int64_t>::max)() - dtNanos) {
    nanos_ = (std::numeric_limits<int64_t>::max)();
  } else {
    nanos_ += dtNanos;
  }
  g_clockError = false;
}

int64_t VirtualClock::nowNanos() const noexcept { return nanos_; }

void VirtualClock::reset() noexcept {
  nanos_ = 0;
  g_clockError = false;
}

void motion_setClock(VirtualClock* clock) noexcept { g_clock = clock; }

bool motion_lastClockError() noexcept { return g_clockError; }

int64_t motion_nowNanos() noexcept {
  if (g_clock != nullptr) return g_clock->nowNanos();
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<int64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}
