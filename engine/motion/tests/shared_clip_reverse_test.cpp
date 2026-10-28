// Batch 2 contract tests — T4 GEOMETRY HALF ONLY
// (FR-MOTION-011 / AC-MOTION-035). Graph/reversal behavior is T5's half in
// this same file later and is deliberately NOT tested here.
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: explicit t01 per call,
// no wall clock, no randomness. Follows the engine/motion convention
// (plain main(), RUN_CASE macro, printf PASS/FAIL, nonzero exit on
// failure; cf. tests/layout_interpolation_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T4.md).
//
// ASSUMPTION (namespace): the plan `Interface:` line is unqualified; these
// tests bind to `motion::SharedAnchor` / `motion::SharedFrame` /
// `motion::motion_interpolateShared` per the existing `motion::` header
// convention (cf. motion/layout_adapter.h).
// ASSUMPTION (Box shape): `Box` is the existing layout_adapter box type
// (`motion::Box{float x, y, w, h;}`); field spellings as found there.
// ASSUMPTION (lerp rule): each geometry channel (box x/y/w/h and
// cornerRadius) interpolates linearly: from + (to - from) * clampedT.
// ASSUMPTION (t01 echo): the returned `t01` field echoes the clamped input
// (non-finite input returns the `from` frame with `t01 == 0`).
// ASSUMPTION (clip channels): the clip rect (`Box clip` on the anchors,
// mirrored as `clipX`/`clipY` on the frame) rides the same t01 as the box
// and corner radius; covered by the check_clip_* cases below.
// ASSUMPTION (float epsilon): 1e-5f, mirroring
// layout_interpolation_test.cpp.
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "motion/layout_adapter.h"
#include "motion/shared_element.h"

namespace {

const float kExact = 1e-5f;

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

motion::Box box(float x, float y, float w, float h) {
  motion::Box b;
  b.x = x;
  b.y = y;
  b.w = w;
  b.h = h;
  return b;
}

bool boxNear(const motion::Box& a, const motion::Box& b, float eps) {
  return near(a.x, b.x, eps) && near(a.y, b.y, eps) &&
         near(a.w, b.w, eps) && near(a.h, b.h, eps);
}

motion::SharedAnchor anchor(const std::string& id, const motion::Box& b,
                            float radius) {
  motion::SharedAnchor a;
  a.elementId = id;
  a.route = "/home";
  a.depth = 0;
  a.box = b;
  a.cornerRadius = radius;
  return a;
}

motion::SharedAnchor fromAnchor() {
  return anchor("hero", box(10.0f, 20.0f, 100.0f, 50.0f), 0.0f);
}

motion::SharedAnchor toAnchor() {
  return anchor("hero", box(30.0f, 60.0f, 200.0f, 90.0f), 12.0f);
}

motion::SharedAnchor anchorClip(const std::string& id, const motion::Box& b,
                                const motion::Box& c, float radius) {
  motion::SharedAnchor a = anchor(id, b, radius);
  a.clip = c;
  return a;
}

motion::SharedAnchor fromClipAnchor() {
  return anchorClip("hero", box(10.0f, 20.0f, 100.0f, 50.0f),
                    box(0.0f, 0.0f, 50.0f, 25.0f), 0.0f);
}

motion::SharedAnchor toClipAnchor() {
  return anchorClip("hero", box(30.0f, 60.0f, 200.0f, 90.0f),
                    box(10.0f, 20.0f, 150.0f, 75.0f), 12.0f);
}

// t=0 returns the `from` frame exactly.
bool check_t0_returns_from_exactly() {
  motion::SharedFrame f =
      motion::motion_interpolateShared(fromAnchor(), toAnchor(), 0.0f);
  return boxNear(f.box, box(10.0f, 20.0f, 100.0f, 50.0f), kExact) &&
         near(f.cornerRadius, 0.0f, kExact) && near(f.t01, 0.0f, kExact);
}

// t=1 returns the `to` frame exactly.
bool check_t1_returns_to_exactly() {
  motion::SharedFrame f =
      motion::motion_interpolateShared(fromAnchor(), toAnchor(), 1.0f);
  return boxNear(f.box, box(30.0f, 60.0f, 200.0f, 90.0f), kExact) &&
         near(f.cornerRadius, 12.0f, kExact) && near(f.t01, 1.0f, kExact);
}

// Midpoint equals the lerp of the endpoints on every geometry channel.
bool check_midpoint_equals_lerp_of_endpoints() {
  motion::SharedFrame f =
      motion::motion_interpolateShared(fromAnchor(), toAnchor(), 0.5f);
  return boxNear(f.box, box(20.0f, 40.0f, 150.0f, 70.0f), kExact) &&
         near(f.cornerRadius, 6.0f, kExact) && near(f.t01, 0.5f, kExact);
}

// t=0.25 lerps each geometry channel linearly.
bool check_quarter_t_lerps_each_channel() {
  motion::SharedFrame f = motion::motion_interpolateShared(
      anchor("hero", box(0.0f, 0.0f, 0.0f, 0.0f), 0.0f),
      anchor("hero", box(100.0f, 200.0f, 400.0f, 800.0f), 20.0f), 0.25f);
  return boxNear(f.box, box(25.0f, 50.0f, 100.0f, 200.0f), kExact) &&
         near(f.cornerRadius, 5.0f, kExact) && near(f.t01, 0.25f, kExact);
}

// t<0 clamps to the `from` frame (no extrapolation).
bool check_negative_t_clamps_to_from() {
  motion::SharedFrame f =
      motion::motion_interpolateShared(fromAnchor(), toAnchor(), -2.0f);
  return boxNear(f.box, box(10.0f, 20.0f, 100.0f, 50.0f), kExact) &&
         near(f.cornerRadius, 0.0f, kExact) && near(f.t01, 0.0f, kExact);
}

// t>1 clamps to the `to` frame (no extrapolation).
bool check_above_one_t_clamps_to_to() {
  motion::SharedFrame f =
      motion::motion_interpolateShared(fromAnchor(), toAnchor(), 1.5f);
  return boxNear(f.box, box(30.0f, 60.0f, 200.0f, 90.0f), kExact) &&
         near(f.cornerRadius, 12.0f, kExact) && near(f.t01, 1.0f, kExact);
}

// Non-finite t01 (NaN, +inf, -inf) returns the `from` frame.
bool check_nonfinite_t_returns_from() {
  const float bad[] = {
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),
  };
  for (float t : bad) {
    motion::SharedFrame f =
        motion::motion_interpolateShared(fromAnchor(), toAnchor(), t);
    if (!boxNear(f.box, box(10.0f, 20.0f, 100.0f, 50.0f), kExact))
      return false;
    if (!near(f.cornerRadius, 0.0f, kExact)) return false;
    if (!near(f.t01, 0.0f, kExact)) return false;
  }
  return true;
}

// Identical from/to interpolates to itself at any t.
bool check_identical_anchors_stable() {
  motion::SharedAnchor a =
      anchor("hero", box(5.0f, 5.0f, 50.0f, 50.0f), 4.0f);
  const float ts[] = {0.0f, 0.37f, 1.0f};
  for (float t : ts) {
    motion::SharedFrame f = motion::motion_interpolateShared(a, a, t);
    if (!boxNear(f.box, box(5.0f, 5.0f, 50.0f, 50.0f), kExact)) return false;
    if (!near(f.cornerRadius, 4.0f, kExact)) return false;
  }
  return true;
}

// Clip rect rides the same t01: t=0 returns the `from` clip exactly.
bool check_clip_t0_returns_from_clip_exactly() {
  motion::SharedFrame f = motion::motion_interpolateShared(
      fromClipAnchor(), toClipAnchor(), 0.0f);
  return boxNear(f.clip, box(0.0f, 0.0f, 50.0f, 25.0f), kExact) &&
         near(f.clipX, 0.0f, kExact) && near(f.clipY, 0.0f, kExact);
}

// Clip midpoint equals the lerp of the endpoint clips.
bool check_clip_midpoint_lerps_clip() {
  motion::SharedFrame f = motion::motion_interpolateShared(
      fromClipAnchor(), toClipAnchor(), 0.5f);
  return boxNear(f.clip, box(5.0f, 10.0f, 100.0f, 50.0f), kExact) &&
         near(f.clipX, 5.0f, kExact) && near(f.clipY, 10.0f, kExact);
}

// Clip t=1 returns the `to` clip exactly.
bool check_clip_t1_returns_to_clip_exactly() {
  motion::SharedFrame f = motion::motion_interpolateShared(
      fromClipAnchor(), toClipAnchor(), 1.0f);
  return boxNear(f.clip, box(10.0f, 20.0f, 150.0f, 75.0f), kExact) &&
         near(f.clipX, 10.0f, kExact) && near(f.clipY, 20.0f, kExact);
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
  RUN_CASE(check_t0_returns_from_exactly);
  RUN_CASE(check_t1_returns_to_exactly);
  RUN_CASE(check_midpoint_equals_lerp_of_endpoints);
  RUN_CASE(check_quarter_t_lerps_each_channel);
  RUN_CASE(check_negative_t_clamps_to_from);
  RUN_CASE(check_above_one_t_clamps_to_to);
  RUN_CASE(check_nonfinite_t_returns_from);
  RUN_CASE(check_identical_anchors_stable);
  RUN_CASE(check_clip_t0_returns_from_clip_exactly);
  RUN_CASE(check_clip_midpoint_lerps_clip);
  RUN_CASE(check_clip_t1_returns_to_clip_exactly);
  if (failures > 0) {
    std::printf("shared_clip_reverse_test: %d failing\n", failures);
    return 1;
  }
  std::printf("shared_clip_reverse_test: all passing\n");
  return 0;
}
