#ifndef MOTION_CLOCK_H_
#define MOTION_CLOCK_H_

#include <cstdint>

// Deterministic virtual clock for the animation graph
// (FR-MOTION-009 / AC-MOTION-026).
//
// The graph reads time only through the injected source: when a virtual
// clock is installed via motion_setClock(), motion_nowNanos() returns the
// virtual time, which advances solely via VirtualClock::advanceFrame().
// With no clock installed, motion_nowNanos() reads the wall clock.
// VirtualClock::nowNanos() never observes the wall clock.
//
// Error channel: advanceFrame() with dtNanos <= 0 leaves time unchanged and
// reports via motion_lastClockError(). The flag is set by an invalid step
// and cleared by a valid step or reset(). Nothing here throws.

class VirtualClock {
 public:
  VirtualClock() = default;

  // Advances virtual time by exactly dtNanos. dtNanos <= 0 is ignored
  // (time unchanged) and reported via motion_lastClockError().
  // Never throws.
  void advanceFrame(int64_t dtNanos) noexcept;

  // Current virtual time in nanoseconds since construction/last reset().
  // Never throws.
  int64_t nowNanos() const noexcept;

  // Returns virtual time to zero and clears any pending clock error.
  // Never throws.
  void reset() noexcept;

 private:
  int64_t nanos_ = 0;
};

// Installs the injected time source; nullptr restores the wall clock.
// Never throws, never traps (repeated installs/restores are safe).
void motion_setClock(VirtualClock* clock) noexcept;

// True when the most recent advanceFrame() on any virtual clock was
// rejected (dtNanos <= 0); cleared by a valid step or reset().
// Never throws.
bool motion_lastClockError() noexcept;

// Reads time through the injected source: the installed virtual clock's
// nowNanos(), or the wall clock (steady_clock) when none is installed.
// Never throws.
int64_t motion_nowNanos() noexcept;

#endif  // MOTION_CLOCK_H_
