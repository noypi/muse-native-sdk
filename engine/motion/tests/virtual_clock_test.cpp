// Batch 2 contract tests — T3 (FR-MOTION-009 / AC-MOTION-026, TC-MOTION-026).
// Hermetic: C++17 + motion headers only; no wall-clock reads, no env/config,
// no filesystem, no network, no randomness. Time is observed only through
// VirtualClock::nowNanos(). Follows the engine/motion convention (plain
// main(), RUN_CASE macro, printf PASS/FAIL, nonzero exit on failure; cf.
// tests/retarget_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T3.md):
//   class VirtualClock { public: void advanceFrame(int64_t dtNanos);
//     int64_t nowNanos() const; void reset(); };
//   void motion_setClock(VirtualClock* clock);
//   motion_setClock(nullptr) restores the wall clock; dtNanos <= 0 is ignored
//   and reported via bool motion_lastClockError().
//
// ASSUMPTION (error-flag clearing): the plan pins only that dtNanos <= 0 is
// "ignored and reported via motion_lastClockError()". These tests treat the
// flag as set-by-invalid / cleared-by-valid-or-reset; call sites — not
// expectations — will be reconciled if the coder publishes different
// clearing semantics.
// ASSUMPTION (graph coupling): "the graph reads time only through the
// injected source" and "N steps produce exactly N frame evaluations" have no
// graph/eval API in this item's Interface, so they are asserted at the clock
// boundary (exact integer accumulation; zero progress without advanceFrame).
// Wiring the graph to the injected source is coder-owned; end-to-end frame
// counting belongs to the tester layer.
#include <cstdint>
#include <cstdio>

#include "motion/clock.h"

namespace {

// A fresh virtual clock reads zero, and the smallest valid step is accepted
// cleanly with no error reported.
bool check_fresh_clock_reads_zero() {
  VirtualClock c;
  if (c.nowNanos() != 0) return false;
  try {
    c.advanceFrame(1);
  } catch (...) {
    return false;  // never throws
  }
  return c.nowNanos() == 1 && !motion_lastClockError();
}

// One explicit step advances time by exactly that delta (AC-MOTION-026:
// time advances only on explicit frame steps).
bool check_advance_frame_moves_time_forward_by_dt() {
  VirtualClock c;
  const int64_t kDt = 16666666;  // ~60 Hz frame in nanos
  try {
    c.advanceFrame(kDt);
  } catch (...) {
    return false;
  }
  return c.nowNanos() == kDt && !motion_lastClockError();
}

// With the virtual clock installed, no advanceFrame means zero progress:
// the readable time never moves on its own (AC-MOTION-026).
bool check_no_advance_means_zero_progress() {
  VirtualClock c;
  motion_setClock(&c);
  c.advanceFrame(5000000);
  const int64_t frozen = c.nowNanos();
  // Any wall interval: spin without stepping; observable time must not move.
  volatile int spin = 0;
  for (int i = 0; i < 100000; ++i) spin += i;
  (void)spin;
  const bool held = (c.nowNanos() == frozen);
  motion_setClock(nullptr);
  return held;
}

// N explicit steps accumulate to exactly N*dt (integer-exact, deterministic;
// AC-MOTION-026 "N steps produce exactly N frame evaluations" at the clock
// boundary — each advanceFrame contributes exactly one frame's worth of
// time, so the Nth evaluation reads exactly N*dt).
bool check_n_steps_accumulate_to_exactly_n_times_dt() {
  VirtualClock c;
  const int64_t kDt = 8333333;  // ~120 Hz frame in nanos
  const int kSteps = 120;
  for (int i = 0; i < kSteps; ++i) c.advanceFrame(kDt);
  return c.nowNanos() == static_cast<int64_t>(kSteps) * kDt;
}

// dtNanos == 0 is ignored: time does not move and the error is reported.
bool check_zero_dt_is_ignored_and_reports_error() {
  VirtualClock c;
  c.advanceFrame(1000);
  const int64_t before = c.nowNanos();
  try {
    c.advanceFrame(0);
  } catch (...) {
    return false;
  }
  return c.nowNanos() == before && motion_lastClockError();
}

// Negative dtNanos values are ignored: time does not move and the error is
// reported (boundary: -1 and INT64_MIN).
bool check_negative_dt_is_ignored_and_reports_error() {
  VirtualClock c;
  c.advanceFrame(1000);
  const int64_t before = c.nowNanos();
  try {
    c.advanceFrame(-1);
    if (c.nowNanos() != before || !motion_lastClockError()) return false;
    c.advanceFrame(INT64_MIN);
  } catch (...) {
    return false;
  }
  return c.nowNanos() == before && motion_lastClockError();
}

// A valid step after an invalid one clears the error flag (ASSUMPTION:
// sticky-until-valid; see file header).
bool check_valid_advance_clears_error() {
  VirtualClock c;
  c.advanceFrame(0);  // establish the error precondition explicitly
  if (!motion_lastClockError()) return false;
  c.advanceFrame(2500);
  return c.nowNanos() == 2500 && !motion_lastClockError();
}

// reset() returns the clock to zero (ASSUMPTION: reset also clears a pending
// clock error; time-zero is the specified behavior).
bool check_reset_returns_clock_to_zero() {
  VirtualClock c;
  c.advanceFrame(987654321);
  if (c.nowNanos() != 987654321) return false;
  c.advanceFrame(0);  // establish error precondition
  c.reset();
  return c.nowNanos() == 0 && !motion_lastClockError();
}

// Installing a virtual clock and restoring with nullptr never traps; a
// repeated nullptr restore is also safe.
bool check_install_and_null_restore_do_not_trap() {
  try {
    VirtualClock c;
    motion_setClock(&c);
    c.advanceFrame(42);
    if (c.nowNanos() != 42) {
      motion_setClock(nullptr);
      return false;
    }
    motion_setClock(nullptr);  // restores the wall clock
    motion_setClock(nullptr);  // repeated restore is safe
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_fresh_clock_reads_zero);
  RUN_CASE(check_advance_frame_moves_time_forward_by_dt);
  RUN_CASE(check_no_advance_means_zero_progress);
  RUN_CASE(check_n_steps_accumulate_to_exactly_n_times_dt);
  RUN_CASE(check_zero_dt_is_ignored_and_reports_error);
  RUN_CASE(check_negative_dt_is_ignored_and_reports_error);
  RUN_CASE(check_valid_advance_clears_error);
  RUN_CASE(check_reset_returns_clock_to_zero);
  RUN_CASE(check_install_and_null_restore_do_not_trap);
  if (failures > 0) {
    std::printf("virtual_clock_test: %d failing\n", failures);
    return 1;
  }
  std::printf("virtual_clock_test: all passing\n");
  return 0;
}
