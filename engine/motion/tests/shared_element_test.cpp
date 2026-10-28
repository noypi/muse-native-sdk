// T3 contract tests — FR-MOTION-003 / AC-007 (TC-MOTION-007).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Time flows only through
// the nowMs parameters; the OS clock is never touched.
// Binds to the T3 `Interface:` lines (reproduced verbatim in the T3 contract).
//
// ASSUMPTION (shared clock/solver): per plan A5/Risks the clock and box
// interpolation live in coder-owned shared headers (proposed names
// `motion/clock.h`, `motion/solver.h`) reused from T2. These cases drive
// time only via startShared/sampleShared nowMs, so those headers may be
// named/shaped freely by the coder.
// ASSUMPTION (type spellings): Rect is assumed to be an aggregate with
// float fields x, y, width, height in that order; SurfaceKind is assumed to
// carry at least Nav and PlatformView enumerators; Anchor is assumed to
// carry the registered id, a copy of the stability key, the box and the
// kind. If the coder spells these differently, update call sites — not the
// expectations — per the reconciliation rule recorded in the T3 contract.
// ASSUMPTION (unknown key sampling): sampling a key that was never started
// (or never matched) returns a zero box {0,0,0,0} without throwing. A throw
// naming the key is also acceptable only if documented; these cases assert
// the non-throwing zero-box form.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>

#include "motion/shared_element.h"

namespace {

bool approx(float a, float b, float eps = 1e-3f) {
  return std::fabs(a - b) <= eps;
}

bool same_rect(const motion::Rect& r, float x, float y, float w, float h) {
  return approx(r.x, x) && approx(r.y, y) && approx(r.width, w) &&
         approx(r.height, h);
}

bool mentions(const std::exception& e, const char* needle) {
  return std::strstr(e.what(), needle) != nullptr;
}

}  // namespace

// Named case: two anchors with the same key in different trees match by key
// (not tree position); out params describe both anchors.
static bool check_same_key_in_different_trees_matches() {
  motion::registerAnchor(101, "hero-card", {0.0f, 0.0f, 100.0f, 100.0f},
                         motion::SurfaceKind::Nav);
  motion::registerAnchor(202, "hero-card", {200.0f, 300.0f, 100.0f, 100.0f},
                         motion::SurfaceKind::Nav);
  motion::Anchor a{};
  motion::Anchor b{};
  if (!motion::matchShared("hero-card", &a, &b)) return false;
  if (!same_rect(a.box, 0.0f, 0.0f, 100.0f, 100.0f)) return false;
  return same_rect(b.box, 200.0f, 300.0f, 100.0f, 100.0f);
}

// Named case: unknown key returns false and leaves callers with a defined
// answer (no match, no crash) — null out params included.
static bool check_unknown_key_returns_false() {
  motion::Anchor a{};
  motion::Anchor b{};
  if (motion::matchShared("motion.nope.no-such-shared", &a, &b)) return false;
  return !motion::matchShared("motion.nope.no-such-shared", nullptr, nullptr);
}

// Named case: a single anchor (missing partner — e.g. before deep-link
// entry materialises) does not match yet.
static bool check_single_anchor_does_not_match() {
  motion::registerAnchor(303, "late-arrival", {10.0f, 10.0f, 50.0f, 50.0f},
                         motion::SurfaceKind::Nav);
  motion::Anchor a{};
  motion::Anchor b{};
  return !motion::matchShared("late-arrival", &a, &b);
}

// Named case: late second anchor still matches (deep-link entry path).
static bool check_late_second_anchor_matches() {
  motion::Anchor a{};
  motion::Anchor b{};
  if (motion::matchShared("late-arrival", &a, &b)) return false;
  motion::registerAnchor(304, "late-arrival", {60.0f, 70.0f, 50.0f, 50.0f},
                         motion::SurfaceKind::Nav);
  if (!motion::matchShared("late-arrival", &a, &b)) return false;
  return same_rect(b.box, 60.0f, 70.0f, 50.0f, 50.0f);
}

// Named case: nav -> platform-view pair matches via the surface rect; the
// platform-view side keeps its kind.
static bool check_nav_to_platform_view_matches_via_surface_rect() {
  motion::registerAnchor(401, "video-hero", {0.0f, 0.0f, 320.0f, 180.0f},
                         motion::SurfaceKind::Nav);
  motion::registerAnchor(402, "video-hero", {0.0f, 400.0f, 320.0f, 180.0f},
                         motion::SurfaceKind::PlatformView);
  motion::Anchor a{};
  motion::Anchor b{};
  if (!motion::matchShared("video-hero", &a, &b)) return false;
  if (a.kind == b.kind) return false;  // sides must differ here.
  if (b.kind != motion::SurfaceKind::PlatformView &&
      a.kind != motion::SurfaceKind::PlatformView)
    return false;
  return same_rect(a.box, 0.0f, 0.0f, 320.0f, 180.0f) ||
         same_rect(b.box, 0.0f, 0.0f, 320.0f, 180.0f);
}

// Named case: TC-MOTION-007 interpolation — sampleShared moves every box
// corner monotonically from A to B and converges on B.
static bool check_sample_shared_interpolates_monotonically() {
  const char* key = "slide-hero";
  motion::registerAnchor(501, key, {0.0f, 0.0f, 100.0f, 100.0f},
                         motion::SurfaceKind::Nav);
  motion::registerAnchor(502, key, {200.0f, 100.0f, 200.0f, 50.0f},
                         motion::SurfaceKind::Nav);
  motion::startShared(key, "spatial-default", 60000);
  motion::Rect at_start = motion::sampleShared(key, 60000);
  motion::Rect at_mid = motion::sampleShared(key, 60000 + 150);
  motion::Rect at_end = motion::sampleShared(key, 60000 + 100000);
  if (!same_rect(at_start, 0.0f, 0.0f, 100.0f, 100.0f)) return false;
  if (!(at_mid.x > 0.0f && at_mid.x < 200.0f)) return false;
  if (!(at_mid.width > 100.0f && at_mid.width < 200.0f)) return false;
  if (!(at_mid.height < 100.0f && at_mid.height > 50.0f)) return false;
  if (!(at_mid.y >= 0.0f && at_mid.y <= 100.0f)) return false;
  return same_rect(at_end, 200.0f, 100.0f, 200.0f, 50.0f);
}

// Named case: startShared with an unknown token throws naming the token.
static bool check_start_shared_unknown_token_throws() {
  const char* key = "bad-token-hero";
  const char* missing = "motion.nope.missing-token";
  motion::registerAnchor(601, key, {0.0f, 0.0f, 10.0f, 10.0f},
                         motion::SurfaceKind::Nav);
  motion::registerAnchor(602, key, {20.0f, 20.0f, 10.0f, 10.0f},
                         motion::SurfaceKind::Nav);
  try {
    motion::startShared(key, missing, 70000);
  } catch (const std::exception& e) {
    return mentions(e, missing);
  }
  return false;
}

// Named case: sampling before the start time clamps to the first anchor box.
static bool check_sample_before_start_clamps_to_first_box() {
  const char* key = "early-sample";
  motion::registerAnchor(701, key, {5.0f, 5.0f, 40.0f, 40.0f},
                         motion::SurfaceKind::Nav);
  motion::registerAnchor(702, key, {105.0f, 5.0f, 40.0f, 40.0f},
                         motion::SurfaceKind::Nav);
  motion::startShared(key, "spatial-default", 80000);
  motion::Rect early = motion::sampleShared(key, 79000);
  return same_rect(early, 5.0f, 5.0f, 40.0f, 40.0f);
}

// Named case: boundary — sampling a never-started key returns the zero box
// without throwing; null key does not crash matchShared.
static bool check_unstarted_key_returns_zero_box() {
  motion::Rect r;
  try {
    r = motion::sampleShared("motion.nope.never-started", 90000);
  } catch (const std::exception&) {
    return false;
  }
  if (!same_rect(r, 0.0f, 0.0f, 0.0f, 0.0f)) return false;
  motion::Anchor a{};
  motion::Anchor b{};
  try {
    if (motion::matchShared(nullptr, &a, &b)) return false;
  } catch (const std::exception&) {
    return true;  // throwing on null is acceptable; must not crash.
  }
  return true;
}

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_same_key_in_different_trees_matches);
  RUN_CASE(check_unknown_key_returns_false);
  RUN_CASE(check_single_anchor_does_not_match);
  RUN_CASE(check_late_second_anchor_matches);
  RUN_CASE(check_nav_to_platform_view_matches_via_surface_rect);
  RUN_CASE(check_sample_shared_interpolates_monotonically);
  RUN_CASE(check_start_shared_unknown_token_throws);
  RUN_CASE(check_sample_before_start_clamps_to_first_box);
  RUN_CASE(check_unstarted_key_returns_zero_box);
  if (failures > 0) {
    std::printf("shared_element_test: %d failing\n", failures);
    return 1;
  }
  std::printf("shared_element_test: all passing\n");
  return 0;
}
