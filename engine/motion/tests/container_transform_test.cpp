// T8 container half — FR-MOTION-012 / AC-MOTION-040.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: explicit
// t01 per call, no wall clock, no randomness.
// Binds to the T8 `Interface:` line (reproduced verbatim in unit-tests/T8.md).
//
// Greenfield `motion/large_motion.h` — this file must FAIL TO COMPILE until
// the coder adds that header (red state).
//
// ASSUMPTION (container shape): the transform interpolates exactly one
// container box (from→to at t01); per-child boxes are untouched. The test
// passes children explicitly and asserts count + values unchanged.
// ASSUMPTION (epsilon): float comparisons use 1e-5f.
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

#include "motion/layout_adapter.h"
#include "motion/large_motion.h"

namespace {

const float kEps = 1e-5f;
bool near(float a, float b) { return std::fabs(a - b) <= kEps; }

motion::Box box(float x, float y, float w, float h) {
  motion::Box b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  return b;
}

bool boxNear(const motion::Box& a, const motion::Box& b) {
  return near(a.x, b.x) && near(a.y, b.y) && near(a.w, b.w) && near(a.h, b.h);
}

// t=0 returns the from box exactly.
bool check_t0_returns_from() {
  std::vector<motion::Box> kids = {box(1.0f, 2.0f, 3.0f, 4.0f)};
  motion::Box out = motion::motion_containerTransform(
      kids, box(10.0f, 20.0f, 100.0f, 50.0f),
      box(30.0f, 60.0f, 200.0f, 90.0f), 0.0f);
  return boxNear(out, box(10.0f, 20.0f, 100.0f, 50.0f));
}

// t=1 returns the to box exactly.
bool check_t1_returns_to() {
  std::vector<motion::Box> kids = {box(1.0f, 2.0f, 3.0f, 4.0f)};
  motion::Box out = motion::motion_containerTransform(
      kids, box(10.0f, 20.0f, 100.0f, 50.0f),
      box(30.0f, 60.0f, 200.0f, 90.0f), 1.0f);
  return boxNear(out, box(30.0f, 60.0f, 200.0f, 90.0f));
}

// t=0.5 returns the per-channel midpoint of the container box.
bool check_t05_returns_midpoint() {
  std::vector<motion::Box> kids = {box(1.0f, 2.0f, 3.0f, 4.0f)};
  motion::Box out = motion::motion_containerTransform(
      kids, box(10.0f, 20.0f, 100.0f, 50.0f),
      box(30.0f, 60.0f, 200.0f, 90.0f), 0.5f);
  return boxNear(out, box(20.0f, 40.0f, 150.0f, 70.0f));
}

// Child count is unchanged by the transform (input vector untouched).
bool check_child_count_unchanged() {
  std::vector<motion::Box> kids = {box(1.0f, 2.0f, 3.0f, 4.0f),
                                   box(5.0f, 6.0f, 7.0f, 8.0f),
                                   box(9.0f, 10.0f, 11.0f, 12.0f)};
  const std::size_t n = kids.size();
  motion::motion_containerTransform(
      kids, box(0.0f, 0.0f, 100.0f, 100.0f),
      box(50.0f, 50.0f, 200.0f, 200.0f), 0.5f);
  return kids.size() == n;
}

// Per-child boxes are unmoved by the transform.
bool check_children_unmoved() {
  std::vector<motion::Box> kids = {box(1.0f, 2.0f, 3.0f, 4.0f),
                                   box(5.0f, 6.0f, 7.0f, 8.0f)};
  std::vector<motion::Box> snapshot = kids;
  motion::motion_containerTransform(
      kids, box(0.0f, 0.0f, 100.0f, 100.0f),
      box(50.0f, 50.0f, 200.0f, 200.0f), 0.5f);
  return boxNear(kids[0], snapshot[0]) && boxNear(kids[1], snapshot[1]);
}

// Security: non-finite t01 maps to the `from` endpoint (matching the
// motion_interpolateShared NaN→from convention) — NaN never escapes.
bool check_nan_t01_returns_from() {
  std::vector<motion::Box> kids = {box(1.0f, 2.0f, 3.0f, 4.0f)};
  const float nan = std::numeric_limits<float>::quiet_NaN();
  motion::Box out = motion::motion_containerTransform(
      kids, box(10.0f, 20.0f, 100.0f, 50.0f),
      box(30.0f, 60.0f, 200.0f, 90.0f), nan);
  if (!std::isfinite(out.x) || !std::isfinite(out.y) ||
      !std::isfinite(out.w) || !std::isfinite(out.h))
    return false;
  return boxNear(out, box(10.0f, 20.0f, 100.0f, 50.0f));
}

// Empty children: container still interpolates, exactly one box out.
bool check_empty_children_still_interpolates() {
  std::vector<motion::Box> kids;
  motion::Box out = motion::motion_containerTransform(
      kids, box(0.0f, 0.0f, 0.0f, 0.0f),
      box(100.0f, 200.0f, 400.0f, 800.0f), 0.25f);
  return boxNear(out, box(25.0f, 50.0f, 100.0f, 200.0f)) && kids.empty();
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
  RUN_CASE(check_child_count_unchanged);
  RUN_CASE(check_children_unmoved);
  RUN_CASE(check_empty_children_still_interpolates);
  RUN_CASE(check_nan_t01_returns_from);
  if (failures > 0) {
    std::printf("container_transform_test: %d failing\n", failures);
    return 1;
  }
  std::printf("container_transform_test: all passing\n");
  return 0;
}
