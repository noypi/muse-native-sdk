// T11 contract tests — FR-LISTS-006 / AC-LISTS-028 (TC-LISTS-028).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T11 `Interface:` lines:
//   `A11yActions GetScrollActions(const CollectionNode& n);`
//   returns `{canScrollForward, canScrollBackward, scrollable: true}`
//   even when realizedCount < totalCount +
//   `ScrollResult PerformScrollAction(CollectionNode& n, ScrollDir d);`
//   moves the window, returns new leading index.
// Detail under test: windowed realization never clears the scrollable flag;
// supplies collection node data to the A11Y-SEMANTICS mirror-thread shape
// (opaque sink, no mirror-thread code).
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace, enum spelling, or struct shapes. This test proposes:
// namespace `lists`; `enum class ScrollDir{Forward, Backward}`;
// `struct CollectionNode{int totalCount; int realizedCount; int leadingIndex;}`;
// `struct A11yActions{bool canScrollForward; bool canScrollBackward; bool scrollable;}`;
// `struct ScrollResult{int leadingIndex; bool moved;}`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (scrollable always true;
// windowed 10k exposes actions; scroll moves the window).
//
// ASSUMPTION (edge actions): at the start (leadingIndex 0) canScrollBackward
// is false; at the end canScrollForward is false; a mid-window node exposes
// both. `scrollable` is true in all cases, including empty and fully
// realized collections.
//
// ASSUMPTION (window step): PerformScrollAction moves leadingIndex by one
// realized window (realizedCount), clamped to [0, totalCount - realizedCount];
// already-at-edge scrolls return moved == false with unchanged leadingIndex.
#include <cstdio>

#include "lists/collection_semantics.h"

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

lists::CollectionNode windowed10k(int leading) {
  lists::CollectionNode n;
  n.totalCount = 10000;
  n.realizedCount = 20;
  n.leadingIndex = leading;
  return n;
}

void case_mid_window_both_actions() {
  auto n = windowed10k(5000);
  lists::A11yActions a = lists::GetScrollActions(n);
  check(a.canScrollForward, "mid_can_forward");
  check(a.canScrollBackward, "mid_can_backward");
  check(a.scrollable, "mid_scrollable");
}

void case_start_no_backward() {
  auto n = windowed10k(0);
  lists::A11yActions a = lists::GetScrollActions(n);
  check(a.canScrollForward, "start_can_forward");
  check(!a.canScrollBackward, "start_no_backward");
  check(a.scrollable, "start_scrollable");
}

void case_end_no_forward() {
  auto n = windowed10k(10000 - 20);
  lists::A11yActions a = lists::GetScrollActions(n);
  check(!a.canScrollForward, "end_no_forward");
  check(a.canScrollBackward, "end_can_backward");
  check(a.scrollable, "end_scrollable");
}

void case_windowed_keeps_scrollable() {
  // realizedCount < totalCount must never clear the scrollable flag.
  auto n = windowed10k(0);
  check(n.realizedCount < n.totalCount, "windowed_realized_lt_total");
  check(lists::GetScrollActions(n).scrollable, "windowed_scrollable_true");
}

void case_empty_still_scrollable() {
  lists::CollectionNode n{0, 0, 0};
  lists::A11yActions a = lists::GetScrollActions(n);
  check(!a.canScrollForward, "empty_no_forward");
  check(!a.canScrollBackward, "empty_no_backward");
  check(a.scrollable, "empty_scrollable");
}

void case_fully_realized_still_scrollable() {
  lists::CollectionNode n{10, 10, 0};
  lists::A11yActions a = lists::GetScrollActions(n);
  check(!a.canScrollForward, "full_no_forward");
  check(!a.canScrollBackward, "full_no_backward");
  check(a.scrollable, "full_scrollable");
}

void case_scroll_forward_moves_window() {
  auto n = windowed10k(0);
  lists::ScrollResult r = lists::PerformScrollAction(n, lists::ScrollDir::Forward);
  check(r.moved, "forward_moved");
  check(r.leadingIndex == 20, "forward_new_leading");
  check(n.leadingIndex == 20, "forward_window_moved");
}

void case_scroll_backward_moves_window() {
  auto n = windowed10k(40);
  lists::ScrollResult r = lists::PerformScrollAction(n, lists::ScrollDir::Backward);
  check(r.moved, "backward_moved");
  check(r.leadingIndex == 20, "backward_new_leading");
  check(n.leadingIndex == 20, "backward_window_moved");
}

void case_scroll_clamps_at_end() {
  auto n = windowed10k(10000 - 20);
  lists::ScrollResult r = lists::PerformScrollAction(n, lists::ScrollDir::Forward);
  check(!r.moved, "end_forward_not_moved");
  check(r.leadingIndex == n.leadingIndex, "end_forward_leading_unchanged");
}

void case_scroll_clamps_at_start() {
  auto n = windowed10k(0);
  lists::ScrollResult r = lists::PerformScrollAction(n, lists::ScrollDir::Backward);
  check(!r.moved, "start_backward_not_moved");
  check(r.leadingIndex == 0, "start_backward_leading_unchanged");
}

}  // namespace

int main() {
  case_mid_window_both_actions();
  case_start_no_backward();
  case_end_no_forward();
  case_windowed_keeps_scrollable();
  case_empty_still_scrollable();
  case_fully_realized_still_scrollable();
  case_scroll_forward_moves_window();
  case_scroll_backward_moves_window();
  case_scroll_clamps_at_end();
  case_scroll_clamps_at_start();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
