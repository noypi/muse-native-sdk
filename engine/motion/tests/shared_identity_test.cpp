// Batch 2 contract tests — T3 (FR-MOTION-011 / AC-MOTION-034).
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: no clock, no randomness.
// Follows the engine/motion convention (plain main(), RUN_CASE macro,
// printf PASS/FAIL, nonzero exit on failure).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T3.md).
//
// ASSUMPTION (namespace): the plan `Interface:` line is unqualified; these
// tests bind to `motion::SharedAnchor` / `motion::motion_matchShared` per the
// existing `motion::` header convention (cf. motion/layout_adapter.h).
// ASSUMPTION (Box shape): `Box` is the existing layout_adapter box type
// (`motion::Box{float x, y, w, h;}`); field spellings as found there.
// ASSUMPTION (match side): the returned pointer addresses the `to` entry
// with the requested id (deep-link: empty `from` still matches when `to`
// holds the id); `nullptr` when no `to` entry carries the id.
// ASSUMPTION (float epsilon): exact-match cases use 1e-5f, mirroring
// layout_interpolation_test.cpp.
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

motion::SharedAnchor anchor(const std::string& id, const std::string& route,
                            int depth, const motion::Box& b, float radius) {
  motion::SharedAnchor a;
  a.elementId = id;
  a.route = route;
  a.depth = depth;
  a.box = b;
  a.cornerRadius = radius;
  return a;
}

// Reordered trees: match follows the id, not the position.
bool check_reordered_trees_match_follows_id() {
  std::vector<motion::SharedAnchor> from = {
      anchor("a", "/home", 0, box(0.0f, 0.0f, 10.0f, 10.0f), 0.0f),
      anchor("b", "/home", 1, box(10.0f, 10.0f, 20.0f, 20.0f), 2.0f),
      anchor("c", "/home", 1, box(30.0f, 30.0f, 40.0f, 40.0f), 4.0f),
  };
  std::vector<motion::SharedAnchor> to = {
      anchor("c", "/detail", 1, box(300.0f, 300.0f, 44.0f, 44.0f), 5.0f),
      anchor("a", "/detail", 0, box(0.0f, 0.0f, 12.0f, 12.0f), 1.0f),
      anchor("b", "/detail", 2, box(100.0f, 100.0f, 22.0f, 22.0f), 3.0f),
  };
  const motion::SharedAnchor* got =
      motion::motion_matchShared(from, to, "b");
  if (got == nullptr) return false;
  if (got != &to[2]) return false;  // addresses the `to` entry, not `from`
  return got->elementId == "b" && got->depth == 2 &&
         near(got->box.x, 100.0f, kExact);
}

// Nested depths: same id at different depths/routes still matches, and the
// `to`-side record (depth, route, box) is what comes back.
bool check_nested_depths_match_returns_to_record() {
  std::vector<motion::SharedAnchor> from = {
      anchor("hero", "/home", 0, box(0.0f, 0.0f, 50.0f, 50.0f), 0.0f),
  };
  std::vector<motion::SharedAnchor> to = {
      anchor("hero", "/home/detail", 3, box(5.0f, 5.0f, 60.0f, 60.0f), 8.0f),
  };
  const motion::SharedAnchor* got =
      motion::motion_matchShared(from, to, "hero");
  if (got == nullptr) return false;
  if (got != &to[0]) return false;
  return got->route == "/home/detail" && got->depth == 3 &&
         near(got->cornerRadius, 8.0f, kExact);
}

// Deep link: empty `from` still matches when `to` holds the id.
bool check_empty_from_deep_link_matches_to() {
  std::vector<motion::SharedAnchor> from;
  std::vector<motion::SharedAnchor> to = {
      anchor("hero", "/detail", 0, box(7.0f, 8.0f, 90.0f, 90.0f), 6.0f),
  };
  const motion::SharedAnchor* got =
      motion::motion_matchShared(from, to, "hero");
  if (got == nullptr) return false;
  return got == &to[0] && got->elementId == "hero";
}

// Absent id: no `to` entry carries it, so the result is nullptr — even when
// `from` holds that id.
bool check_absent_id_returns_nullptr() {
  std::vector<motion::SharedAnchor> from = {
      anchor("gone", "/home", 0, box(0.0f, 0.0f, 10.0f, 10.0f), 0.0f),
  };
  std::vector<motion::SharedAnchor> to = {
      anchor("other", "/detail", 0, box(1.0f, 1.0f, 11.0f, 11.0f), 1.0f),
  };
  if (motion::motion_matchShared(from, to, "gone") != nullptr) return false;
  // Both sides empty: still nullptr, never a dangling pointer.
  std::vector<motion::SharedAnchor> empty;
  return motion::motion_matchShared(empty, empty, "gone") == nullptr;
}

// Empty `to`: nothing to match against, so nullptr.
bool check_empty_to_returns_nullptr() {
  std::vector<motion::SharedAnchor> from = {
      anchor("hero", "/home", 0, box(0.0f, 0.0f, 10.0f, 10.0f), 0.0f),
  };
  std::vector<motion::SharedAnchor> to;
  return motion::motion_matchShared(from, to, "hero") == nullptr;
}

// The matcher is noexcept: it must never throw, even on empty inputs.
bool check_match_never_throws() {
  return noexcept(motion::motion_matchShared(
      std::declval<const std::vector<motion::SharedAnchor>&>(),
      std::declval<const std::vector<motion::SharedAnchor>&>(),
      std::declval<const std::string&>()));
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
  RUN_CASE(check_reordered_trees_match_follows_id);
  RUN_CASE(check_nested_depths_match_returns_to_record);
  RUN_CASE(check_empty_from_deep_link_matches_to);
  RUN_CASE(check_absent_id_returns_nullptr);
  RUN_CASE(check_empty_to_returns_nullptr);
  RUN_CASE(check_match_never_throws);
  if (failures > 0) {
    std::printf("shared_identity_test: %d failing\n", failures);
    return 1;
  }
  std::printf("shared_identity_test: all passing\n");
  return 0;
}
