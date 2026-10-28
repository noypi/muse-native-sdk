// T8 duration half — FR-MOTION-012 / AC-MOTION-039.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: pure
// function of explicit inputs, no wall clock, no randomness.
// Binds to the T8 `Interface:` line (reproduced verbatim in unit-tests/T8.md).
//
// Greenfield `motion/large_motion.h` — this file must FAIL TO COMPILE until
// the coder adds that header (red state).
//
// ASSUMPTION (bound values): minMs/maxMs are test-chosen token-style bounds,
// not spec values; the contract is clamp-into-[minMs,maxMs], not any number.
// ASSUMPTION (epsilon): float comparisons use 1e-3f.
#include <cmath>
#include <cstdio>
#include <limits>

#include "motion/large_motion.h"

namespace {

const float kEps = 1e-3f;
bool near(float a, float b) { return std::fabs(a - b) <= kEps; }

// Monotonic: compact <= medium <= expanded for a fixed base in range.
bool check_monotonic_compact_to_expanded() {
  const float base = 300.0f, lo = 100.0f, hi = 600.0f;
  float c = motion::motion_scaledDuration(base, motion::SizeClass::Compact, lo, hi);
  float m = motion::motion_scaledDuration(base, motion::SizeClass::Medium, lo, hi);
  float e = motion::motion_scaledDuration(base, motion::SizeClass::Expanded, lo, hi);
  return c <= m + kEps && m <= e + kEps;
}

// All three results stay within [minMs, maxMs].
bool check_results_within_bounds() {
  const float base = 300.0f, lo = 100.0f, hi = 600.0f;
  float c = motion::motion_scaledDuration(base, motion::SizeClass::Compact, lo, hi);
  float m = motion::motion_scaledDuration(base, motion::SizeClass::Medium, lo, hi);
  float e = motion::motion_scaledDuration(base, motion::SizeClass::Expanded, lo, hi);
  return c >= lo - kEps && c <= hi + kEps && m >= lo - kEps && m <= hi + kEps &&
         e >= lo - kEps && e <= hi + kEps;
}

// Tiny base clamps at the lower bound for every size class.
bool check_clamps_at_lower_bound() {
  const float lo = 100.0f, hi = 600.0f;
  float c = motion::motion_scaledDuration(1.0f, motion::SizeClass::Compact, lo, hi);
  float m = motion::motion_scaledDuration(1.0f, motion::SizeClass::Medium, lo, hi);
  float e = motion::motion_scaledDuration(1.0f, motion::SizeClass::Expanded, lo, hi);
  return near(c, lo) && near(m, lo) && near(e, lo);
}

// Huge base clamps at the upper bound for every size class.
bool check_clamps_at_upper_bound() {
  const float lo = 100.0f, hi = 600.0f;
  float c = motion::motion_scaledDuration(100000.0f, motion::SizeClass::Compact, lo, hi);
  float m = motion::motion_scaledDuration(100000.0f, motion::SizeClass::Medium, lo, hi);
  float e = motion::motion_scaledDuration(100000.0f, motion::SizeClass::Expanded, lo, hi);
  return near(c, hi) && near(m, hi) && near(e, hi);
}

// Base exactly at min stays at min (no underflow below the bound).
bool check_base_at_min_stays_at_min() {
  const float lo = 150.0f, hi = 500.0f;
  float c = motion::motion_scaledDuration(lo, motion::SizeClass::Compact, lo, hi);
  return c >= lo - kEps && c <= hi + kEps;
}

// Security: NaN baseMs must not escape the advertised [minMs,maxMs] bound —
// the contract guarantees clamp-into-bounds, so NaN maps to a bound.
bool check_nan_base_clamped_into_bounds() {
  const float lo = 100.0f, hi = 600.0f;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  float c = motion::motion_scaledDuration(nan, motion::SizeClass::Compact, lo, hi);
  float m = motion::motion_scaledDuration(nan, motion::SizeClass::Medium, lo, hi);
  float e = motion::motion_scaledDuration(nan, motion::SizeClass::Expanded, lo, hi);
  if (!std::isfinite(c) || !std::isfinite(m) || !std::isfinite(e)) return false;
  return c >= lo - kEps && c <= hi + kEps && m >= lo - kEps &&
         m <= hi + kEps && e >= lo - kEps && e <= hi + kEps;
}

// Base exactly at max stays at max (no overflow above the bound).
bool check_base_at_max_stays_at_max() {
  const float lo = 150.0f, hi = 500.0f;
  float e = motion::motion_scaledDuration(hi, motion::SizeClass::Expanded, lo, hi);
  return near(e, hi);
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_monotonic_compact_to_expanded);
  RUN_CASE(check_results_within_bounds);
  RUN_CASE(check_clamps_at_lower_bound);
  RUN_CASE(check_clamps_at_upper_bound);
  RUN_CASE(check_base_at_min_stays_at_min);
  RUN_CASE(check_base_at_max_stays_at_max);
  RUN_CASE(check_nan_base_clamped_into_bounds);
  if (failures > 0) {
    std::printf("size_scaled_motion_test: %d failing\n", failures);
    return 1;
  }
  std::printf("size_scaled_motion_test: all passing\n");
  return 0;
}
