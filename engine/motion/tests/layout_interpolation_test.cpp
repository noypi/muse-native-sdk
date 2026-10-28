// T5 contract tests — FR-MOTION-006 / AC-MOTION-012 (TC-MOTION-012).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: explicit
// t01 per call, no wall clock, no randomness.
// Binds to the T5 `Interface:` line (reproduced verbatim in unit-tests/T5.md).
//
// ASSUMPTION (struct shape): the plan Detail names `Box{float x,y,w,h;}` in
// `motion::` with `interpolateBox(from, to, t01) noexcept`. Field/type
// spellings below are the test's proposal; if the coder names them
// differently, update call sites — not expectations — per reconciliation.
//
// ASSUMPTION (extra channel): corner-radius/clip carried as an extra float
// channel "if present". These tests cover the four named channels exactly;
// if the coder adds a radius/clip channel, its lerp is covered by the same
// linear rule and reconciled without changing these expectations.
#include <cmath>
#include <cstdio>

#include "motion/layout_adapter.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
const float kExact = 1e-5f;

motion::Box box(float x, float y, float w, float h) {
  motion::Box b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  return b;
}

bool boxNear(const motion::Box& a, const motion::Box& b, float eps) {
  return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.w, b.w, eps) &&
         near(a.h, b.h, eps);
}

// t=0 returns from exactly.
bool check_t0_returns_from() {
  motion::Box mid =
      motion::interpolateBox(box(10.0f, 20.0f, 100.0f, 50.0f),
                             box(30.0f, 60.0f, 200.0f, 90.0f), 0.0f);
  return boxNear(mid, box(10.0f, 20.0f, 100.0f, 50.0f), kExact);
}

// t=1 returns to exactly.
bool check_t1_returns_to() {
  motion::Box mid =
      motion::interpolateBox(box(10.0f, 20.0f, 100.0f, 50.0f),
                             box(30.0f, 60.0f, 200.0f, 90.0f), 1.0f);
  return boxNear(mid, box(30.0f, 60.0f, 200.0f, 90.0f), kExact);
}

// t=0.5 returns per-channel midpoint exactly.
bool check_t05_returns_midpoint() {
  motion::Box mid =
      motion::interpolateBox(box(10.0f, 20.0f, 100.0f, 50.0f),
                             box(30.0f, 60.0f, 200.0f, 90.0f), 0.5f);
  return boxNear(mid, box(20.0f, 40.0f, 150.0f, 70.0f), kExact);
}

// t=0.25 lerps each channel linearly.
bool check_t025_linear_each_channel() {
  motion::Box mid = motion::interpolateBox(box(0.0f, 0.0f, 0.0f, 0.0f),
                                           box(100.0f, 200.0f, 400.0f, 800.0f),
                                           0.25f);
  return boxNear(mid, box(25.0f, 50.0f, 100.0f, 200.0f), kExact);
}

// t<0 clamps to from (no extrapolation).
bool check_negative_t_clamps_to_from() {
  motion::Box mid =
      motion::interpolateBox(box(10.0f, 20.0f, 100.0f, 50.0f),
                             box(30.0f, 60.0f, 200.0f, 90.0f), -2.0f);
  return boxNear(mid, box(10.0f, 20.0f, 100.0f, 50.0f), kExact);
}

// t>1 clamps to to (no extrapolation).
bool check_above_one_t_clamps_to_to() {
  motion::Box mid =
      motion::interpolateBox(box(10.0f, 20.0f, 100.0f, 50.0f),
                             box(30.0f, 60.0f, 200.0f, 90.0f), 1.5f);
  return boxNear(mid, box(30.0f, 60.0f, 200.0f, 90.0f), kExact);
}

// Identical from/to interpolates to itself at any t.
bool check_identical_boxes_stable() {
  motion::Box b = box(5.0f, 5.0f, 50.0f, 50.0f);
  return boxNear(motion::interpolateBox(b, b, 0.0f), b, kExact) &&
         boxNear(motion::interpolateBox(b, b, 0.37f), b, kExact) &&
         boxNear(motion::interpolateBox(b, b, 1.0f), b, kExact);
}

// Zero-size box lerps without NaN (degenerate geometry).
bool check_zero_size_box_no_nan() {
  motion::Box mid = motion::interpolateBox(box(0.0f, 0.0f, 0.0f, 0.0f),
                                           box(10.0f, 10.0f, 0.0f, 0.0f),
                                           0.5f);
  motion::Box want = box(5.0f, 5.0f, 0.0f, 0.0f);
  return boxNear(mid, want, kExact) && std::isfinite(mid.x) &&
         std::isfinite(mid.y) && std::isfinite(mid.w) && std::isfinite(mid.h);
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
  RUN_CASE(check_t0_returns_from);
  RUN_CASE(check_t1_returns_to);
  RUN_CASE(check_t05_returns_midpoint);
  RUN_CASE(check_t025_linear_each_channel);
  RUN_CASE(check_negative_t_clamps_to_from);
  RUN_CASE(check_above_one_t_clamps_to_to);
  RUN_CASE(check_identical_boxes_stable);
  RUN_CASE(check_zero_size_box_no_nan);
  if (failures > 0) {
    std::printf("layout_interpolation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("layout_interpolation_test: all passing\n");
  return 0;
}
