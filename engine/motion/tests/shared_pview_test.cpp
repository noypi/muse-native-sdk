// Batch 3 contract tests — T5 (graph + reversal half) and T6 (surface-rect
// half) share this file per the plan batch table
// (FR-MOTION-011 / AC-MOTION-035, AC-MOTION-036, AC-MOTION-037).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. Deterministic: explicit
// progress/t01 inputs per call, no wall clock, no randomness. Follows the
// engine/motion convention (plain main(), RUN_CASE macro, printf PASS/FAIL,
// nonzero exit on failure; cf. tests/shared_identity_test.cpp).
//
// Binds to the plan items' `Interface:` lines (reproduced verbatim in
// unit-tests/T5.md and unit-tests/T6.md).
//
// ASSUMPTION (namespace): the plan `Interface:` lines are unqualified; these
// tests bind to `motion::NodeId` / `motion::Graph` (motion/graph.h),
// `motion::TransitionSpec` (motion/implicit.h: `TransitionSpec{SpringSpec
// spring; float delayMs; float staggerMs;}`), `motion::SharedAnchor` /
// `motion::SharedFrame` (motion/shared_element.h), and `motion::Box`
// (motion/layout_adapter.h: `Box{float x, y, w, h;}`) per the existing
// `motion::` header convention. Field/type spellings are the test's
// proposal — update call sites, not expectations, if the coder names them
// differently.
// ASSUMPTION (T5 node presence): the NodeId returned by
// motion_sharedTransition addresses a node in `g`, so `g.evaluate(tMs)`
// reports it in `Sample::values` (unknown-id reads are ignored per
// graph.h, so presence proves registration).
// ASSUMPTION (T5 drive meaning): motion_driveShared(backProgress01)
// returns the current scrub position t01 in [0,1] after absorbing the
// driven progress; repeated drive(1.0f) converges to 1 within 200 calls
// (settle eps 1e-2 on t01) and repeated drive(0.0f) converges to 0.
// ASSUMPTION (T5 clamp): out-of-range progress clamps (drive(-0.5f) reads
// as 0, drive(2.0f) reads as 1); each returned value stays in [0,1].
// ASSUMPTION (T5 continuity-jump threshold): scrubbing never teleports —
// the frame-to-frame position travel is bounded by the driven progress
// delta times the total endpoint distance, plus kSlack (1e-3f) for solver
// smoothing lag. Smoothing can only shrink deltas, so this bound is
// order/state independent.
// ASSUMPTION (T6 wrap): motion_pviewAnchor copies its inputs verbatim
// (elementId, route, surfaceRect -> box, cornerRadius) with no validation
// and never throws, even for empty ids.
// ASSUMPTION (float epsilon): exact-match cases use 1e-5f, mirroring
// layout_interpolation_test.cpp; settle eps 1e-2 on t01.
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "motion/graph.h"
#include "motion/implicit.h"
#include "motion/layout_adapter.h"
#include "motion/shared_element.h"

namespace {

const float kExact = 1e-5f;
const float kSettle = 1e-2f;
const float kSlack = 1e-3f;

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

float boxDist(const motion::Box& a, const motion::Box& b) {
  float dx = a.x - b.x;
  float dy = a.y - b.y;
  float dw = a.w - b.w;
  float dh = a.h - b.h;
  return std::sqrt(dx * dx + dy * dy + dw * dw + dh * dh);
}

motion::SharedAnchor anchor(const std::string& id, const std::string& route,
                            const motion::Box& b, float radius) {
  motion::SharedAnchor a;
  a.elementId = id;
  a.route = route;
  a.depth = 0;
  a.box = b;
  a.cornerRadius = radius;
  return a;
}

motion::TransitionSpec springSpec(float stiffness, float dampingRatio) {
  motion::TransitionSpec s;
  s.spring.stiffness = stiffness;
  s.spring.dampingRatio = dampingRatio;
  s.delayMs = 0.0f;
  s.staggerMs = 0.0f;
  return s;
}

const motion::Box kFromBox = box(0.0f, 0.0f, 100.0f, 100.0f);
const motion::Box kToBox = box(200.0f, 400.0f, 300.0f, 500.0f);

float totalTol() { return boxDist(kFromBox, kToBox) * kSettle + kSlack; }

// ---- T5: two-anchor transition node in the existing Graph ----

// The transition call registers a node in the caller's graph: the returned
// id is reported by evaluate (unknown ids are ignored per graph.h, so
// presence proves registration).
bool check_shared_transition_creates_node_in_graph() {
  motion::Graph g;
  motion::SharedAnchor from = anchor("hero", "/list", kFromBox, 4.0f);
  motion::SharedAnchor to = anchor("hero", "/detail", kToBox, 16.0f);
  motion::NodeId id =
      motion::motion_sharedTransition(g, from, to, springSpec(180.0f, 1.0f));
  motion::Sample s0 = g.evaluate(0.0f);
  if (s0.values.count(id) != 1) return false;
  motion::Sample s1 = g.evaluate(5000.0f);
  return s1.values.count(id) == 1;
}

// ---- T5: back-progress scrub 0 -> 0.6 -> 0.2 -> 1 is continuous ----

// Forward scrub makes progress: driving 0 then 0.6 moves the frame from the
// `from` box toward the `to` box with no snap at either end.
bool check_drive_shared_forward_scrub_progresses_without_snap() {
  motion::Graph g;
  motion::SharedAnchor from = anchor("hero", "/list", kFromBox, 4.0f);
  motion::SharedAnchor to = anchor("hero", "/detail", kToBox, 16.0f);
  motion::motion_sharedTransition(g, from, to, springSpec(180.0f, 1.0f));
  float t0 = motion::motion_driveShared(0.0f);
  float t1 = motion::motion_driveShared(0.6f);
  if (t0 < 0.0f || t0 > 1.0f || t1 < 0.0f || t1 > 1.0f) return false;
  if (!(t1 > t0)) return false;  // forward drive moves forward
  motion::SharedFrame f0 = motion::motion_interpolateShared(from, to, t0);
  motion::SharedFrame f1 = motion::motion_interpolateShared(from, to, t1);
  const float total = boxDist(kFromBox, kToBox);
  // Travel bounded by the driven delta: no teleport beyond what was asked.
  return boxDist(f0.box, f1.box) <= std::fabs(t1 - t0) * total + kSlack;
}

// Interruption/reversal: 0 -> 0.6 -> 0.2 -> 1 stays continuous — no
// position jump beyond epsilon at the reversal point, and every probed
// frame stays inside the endpoint span.
bool check_drive_shared_reversal_is_continuous() {
  motion::Graph g;
  motion::SharedAnchor from = anchor("hero", "/list", kFromBox, 4.0f);
  motion::SharedAnchor to = anchor("hero", "/detail", kToBox, 16.0f);
  motion::motion_sharedTransition(g, from, to, springSpec(180.0f, 1.0f));
  const float total = boxDist(kFromBox, kToBox);
  const float progress[] = {0.0f, 0.6f, 0.2f, 1.0f};
  float prevT = motion::motion_driveShared(progress[0]);
  motion::SharedFrame prev =
      motion::motion_interpolateShared(from, to, prevT);
  for (int i = 1; i < 4; ++i) {
    float t = motion::motion_driveShared(progress[i]);
    if (t < 0.0f || t > 1.0f) return false;
    motion::SharedFrame f = motion::motion_interpolateShared(from, to, t);
    // No jump beyond the driven delta (plus solver slack).
    if (boxDist(prev.box, f.box) >
        std::fabs(t - prevT) * total + kSlack)
      return false;
    prevT = t;
    prev = f;
  }
  return true;
}

// The scrub settles at the driven endpoint: repeated drive(1.0f) converges
// to the `to` geometry, repeated drive(0.0f) back to the `from` geometry.
bool check_drive_shared_settles_at_driven_endpoint() {
  motion::Graph g;
  motion::SharedAnchor from = anchor("hero", "/list", kFromBox, 4.0f);
  motion::SharedAnchor to = anchor("hero", "/detail", kToBox, 16.0f);
  motion::motion_sharedTransition(g, from, to, springSpec(180.0f, 1.0f));
  float t = 0.0f;
  for (int i = 0; i < 200; ++i) t = motion::motion_driveShared(1.0f);
  if (!near(t, 1.0f, kSettle)) return false;
  motion::SharedFrame f = motion::motion_interpolateShared(from, to, t);
  if (!boxNear(f.box, kToBox, totalTol())) return false;
  for (int i = 0; i < 200; ++i) t = motion::motion_driveShared(0.0f);
  if (!near(t, 0.0f, kSettle)) return false;
  motion::SharedFrame f0 = motion::motion_interpolateShared(from, to, t);
  return boxNear(f0.box, kFromBox, totalTol());
}

// Boundary: out-of-range progress clamps into [0,1], never escapes.
bool check_drive_shared_clamps_progress() {
  float lo = motion::motion_driveShared(-0.5f);
  float hi = motion::motion_driveShared(2.0f);
  return lo >= 0.0f && lo <= 1.0f && hi >= 0.0f && hi <= 1.0f &&
         near(lo, 0.0f, kSettle) && near(hi, 1.0f, kSettle);
}

// The interruption test is pure function calls: drive is noexcept and a
// repeated identical sequence is deterministic (no input-system code).
bool check_drive_shared_pure_and_deterministic() {
  if (!noexcept(motion::motion_driveShared(0.5f))) return false;
  float a = motion::motion_driveShared(0.3f);
  float b = motion::motion_driveShared(0.3f);
  return near(a, b, kExact);
}

// Drive-velocity continuity: after arming, fine-stepping 0 -> 0.6 -> 0.2
// moves monotonically toward each driven target within epsilon at every
// step — the observable solver state tracks the target, never snapping
// past it or teleporting (this probes the drive path itself, not lerp).
bool check_drive_shared_velocity_continuous_stepwise() {
  motion::motion_resetSharedDrive();
  motion::Graph g;
  motion::SharedAnchor from = anchor("hero", "/list", kFromBox, 4.0f);
  motion::SharedAnchor to = anchor("hero", "/detail", kToBox, 16.0f);
  motion::motion_sharedTransition(g, from, to, springSpec(180.0f, 1.0f));
  float t = motion::motion_driveShared(0.0f);
  if (!near(t, 0.0f, kSettle)) return false;
  const float up[] = {0.15f, 0.3f, 0.45f, 0.6f};
  for (float target : up) {
    float next = motion::motion_driveShared(target);
    if (!near(next, target, kSettle)) return false;
    if (!(next >= t)) return false;  // monotonic toward the rising target
    t = next;
  }
  const float down[] = {0.45f, 0.3f, 0.2f};
  for (float target : down) {
    float next = motion::motion_driveShared(target);
    if (!near(next, target, kSettle)) return false;
    if (!(next <= t)) return false;  // monotonic toward the falling target
    t = next;
  }
  return near(t, 0.2f, kSettle);
}

// ---- T6: platform-view surface-rect anchors ----

// The injected surface rect is wrapped verbatim as a matchable anchor.
bool check_pview_anchor_wraps_surface_rect() {
  if (!noexcept(motion::motion_pviewAnchor(
          std::declval<const std::string&>(),
          std::declval<const std::string&>(),
          std::declval<const motion::Box&>(), 0.0f)))
    return false;
  motion::Box rect = box(10.0f, 20.0f, 320.0f, 180.0f);
  motion::SharedAnchor a = motion::motion_pviewAnchor("vid", "/pview", rect,
                                                      8.0f);
  return a.elementId == "vid" && a.route == "/pview" &&
         boxNear(a.box, rect, kExact) && near(a.cornerRadius, 8.0f, kExact);
}

// list -> pview matches by id through the shared matcher.
bool check_list_to_pview_matches_by_id() {
  std::vector<motion::SharedAnchor> from = {
      anchor("card", "/list", box(0.0f, 0.0f, 120.0f, 80.0f), 4.0f),
  };
  std::vector<motion::SharedAnchor> to = {
      motion::motion_pviewAnchor("card", "/pview",
                                 box(0.0f, 600.0f, 390.0f, 220.0f), 0.0f),
  };
  const motion::SharedAnchor* got =
      motion::motion_matchShared(from, to, "card");
  if (got == nullptr) return false;
  return got == &to[0] && got->elementId == "card";
}

// pview -> list (return navigation) matches by id in the other direction.
bool check_pview_to_list_matches_by_id() {
  std::vector<motion::SharedAnchor> from = {
      motion::motion_pviewAnchor("card", "/pview",
                                 box(0.0f, 600.0f, 390.0f, 220.0f), 0.0f),
  };
  std::vector<motion::SharedAnchor> to = {
      anchor("card", "/list", box(0.0f, 0.0f, 120.0f, 80.0f), 4.0f),
  };
  const motion::SharedAnchor* got =
      motion::motion_matchShared(from, to, "card");
  if (got == nullptr) return false;
  return got == &to[0] && boxNear(got->box, box(0.0f, 0.0f, 120.0f, 80.0f),
                                  kExact);
}

// A pview anchor with an unmatched id does not match.
bool check_pview_mismatch_returns_nullptr() {
  std::vector<motion::SharedAnchor> from = {
      anchor("card", "/list", box(0.0f, 0.0f, 120.0f, 80.0f), 4.0f),
  };
  std::vector<motion::SharedAnchor> to = {
      motion::motion_pviewAnchor("other", "/pview",
                                 box(0.0f, 600.0f, 390.0f, 220.0f), 0.0f),
  };
  return motion::motion_matchShared(from, to, "card") == nullptr;
}

// Geometry between the list anchor and the pview anchor interpolates
// without snap: endpoints exact, midpoint is the lerp, fine steps never
// jump beyond their driven share of the total distance.
bool check_pview_interpolation_has_no_snap() {
  motion::SharedAnchor list =
      anchor("card", "/list", box(0.0f, 0.0f, 120.0f, 80.0f), 4.0f);
  motion::SharedAnchor pview = motion::motion_pviewAnchor(
      "card", "/pview", box(0.0f, 600.0f, 390.0f, 220.0f), 0.0f);
  motion::SharedFrame f0 = motion::motion_interpolateShared(list, pview, 0.0f);
  motion::SharedFrame f1 = motion::motion_interpolateShared(list, pview, 1.0f);
  if (!boxNear(f0.box, list.box, kExact)) return false;
  if (!boxNear(f1.box, pview.box, kExact)) return false;
  motion::SharedFrame mid = motion::motion_interpolateShared(list, pview, 0.5f);
  motion::Box expect;
  expect.x = (list.box.x + pview.box.x) * 0.5f;
  expect.y = (list.box.y + pview.box.y) * 0.5f;
  expect.w = (list.box.w + pview.box.w) * 0.5f;
  expect.h = (list.box.h + pview.box.h) * 0.5f;
  if (!boxNear(mid.box, expect, kExact)) return false;
  if (!near(mid.cornerRadius, (4.0f + 0.0f) * 0.5f, kExact)) return false;
  const float total = boxDist(list.box, pview.box);
  motion::SharedFrame prev = f0;
  for (int i = 1; i <= 20; ++i) {
    float t = static_cast<float>(i) / 20.0f;
    motion::SharedFrame f = motion::motion_interpolateShared(list, pview, t);
    if (boxDist(prev.box, f.box) > 0.05f * total + kSlack) return false;
    prev = f;
  }
  return true;
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
  RUN_CASE(check_shared_transition_creates_node_in_graph);
  RUN_CASE(check_drive_shared_forward_scrub_progresses_without_snap);
  RUN_CASE(check_drive_shared_reversal_is_continuous);
  RUN_CASE(check_drive_shared_settles_at_driven_endpoint);
  RUN_CASE(check_drive_shared_clamps_progress);
  RUN_CASE(check_drive_shared_pure_and_deterministic);
  RUN_CASE(check_drive_shared_velocity_continuous_stepwise);
  RUN_CASE(check_pview_anchor_wraps_surface_rect);
  RUN_CASE(check_list_to_pview_matches_by_id);
  RUN_CASE(check_pview_to_list_matches_by_id);
  RUN_CASE(check_pview_mismatch_returns_nullptr);
  RUN_CASE(check_pview_interpolation_has_no_snap);
  if (failures > 0) {
    std::printf("shared_pview_test: %d failing\n", failures);
    return 1;
  }
  std::printf("shared_pview_test: all passing\n");
  return 0;
}
