// T8 contract tests — FR-LISTS-005 / AC-LISTS-025 (TC-LISTS-025).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T8 `Interface:` line:
//   `StickyFrame UpdateSticky(const StickyState& s, float scrollOffset);`
//   returns `{pinnedId, pushedById, yAdjust}`
// Detail under test: pure function of offset, measures headers only;
// offset change touches at most the two adjacent headers (no relayout storm).
// Geometry inputs consumed from the LAYOUT-M3X delegate shape (opaque here).
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or struct shapes. This test proposes:
// namespace `lists`;
// `struct HeaderGeo{std::string id; float offset; float height;}`;
// `struct StickyState{std::vector<HeaderGeo> headers;}` (sorted by offset);
// `struct StickyFrame{std::string pinnedId; std::string pushedById; float yAdjust;}`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (pin/push-out/replacement frames).
//
// ASSUMPTION (push-out): when the next header is within one header height
// of the pinned header's bottom edge, `pushedById` names the incoming header
// and `yAdjust` is negative (pushed up); otherwise `pushedById` is empty and
// `yAdjust` is 0.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/sticky_header.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::StickyState two_sections() {
  lists::StickyState s;
  s.headers.push_back({"sec-a", 0.0f, 48.0f});
  s.headers.push_back({"sec-b", 1000.0f, 48.0f});
  return s;
}

void case_pins_first_at_top() {
  lists::StickyFrame f = lists::UpdateSticky(two_sections(), 0.0f);
  check(f.pinnedId == "sec-a", "pin_first_at_top");
  check(f.pushedById.empty(), "no_push_far_from_boundary");
  check(f.yAdjust == 0.0f, "no_adjust_far_from_boundary");
}

void case_pins_first_mid_section() {
  lists::StickyFrame f = lists::UpdateSticky(two_sections(), 500.0f);
  check(f.pinnedId == "sec-a", "pin_first_mid_section");
  check(f.pushedById.empty(), "no_push_mid_section");
}

void case_push_out_near_boundary() {
  // 48px header; scroll so next header top is 20px above pinned bottom.
  lists::StickyFrame f = lists::UpdateSticky(two_sections(), 1000.0f - 48.0f + 20.0f);
  check(f.pinnedId == "sec-a", "push_pinned_still_first");
  check(f.pushedById == "sec-b", "push_names_incoming");
  check(f.yAdjust < 0.0f, "push_adjust_negative");
}

void case_replacement_past_boundary() {
  lists::StickyFrame f = lists::UpdateSticky(two_sections(), 1000.0f + 1.0f);
  check(f.pinnedId == "sec-b", "replace_pins_second");
  check(f.pushedById.empty(), "replace_no_push");
  check(f.yAdjust == 0.0f, "replace_no_adjust");
}

void case_empty_state_safe() {
  lists::StickyState s;
  lists::StickyFrame f = lists::UpdateSticky(s, 100.0f);
  check(f.pinnedId.empty(), "empty_no_pinned");
  check(f.pushedById.empty() && f.yAdjust == 0.0f, "empty_no_push");
}

void case_single_header_always_pinned() {
  lists::StickyState s;
  s.headers.push_back({"only", 0.0f, 48.0f});
  lists::StickyFrame f = lists::UpdateSticky(s, 5000.0f);
  check(f.pinnedId == "only", "single_always_pinned");
  check(f.pushedById.empty(), "single_no_push");
}

}  // namespace

int main() {
  case_pins_first_at_top();
  case_pins_first_mid_section();
  case_push_out_near_boundary();
  case_replacement_past_boundary();
  case_empty_state_safe();
  case_single_header_always_pinned();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
