// T13 contract tests — FR-LISTS-006 / AC-LISTS-030 (TC-LISTS-030).
// Hermetic: C++17 + lists header only; no network, no env/config reads,
// no filesystem, no live services, no wall-clock (times passed explicitly).
// Binds to the T13 `Interface:` line:
//   `std::optional<Announcement> MaybeAnnounce(AnnounceState& s,
//    int64_t nowMs, int leadingIndex, int total);`
//   returns an announcement at most once per throttle window with the
//   latest position; fling bursts collapse to one.
// Detail under test: live-region-safe single pending announcement,
// overwritten (not queued) during fling; window constant documented in
// the header.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace or struct shapes. This test proposes:
// namespace `lists`; `struct AnnounceState{int64_t lastEmitMs; bool hasEmitted;}`;
// `struct Announcement{int leadingIndex; int total;}`;
// `inline constexpr int64_t kAnnounceThrottleMs = 500;` in
// `lists/collection_announce.h`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (throttle + fling collapse +
// latest position).
//
// ASSUMPTION (initial state): default-constructed `AnnounceState{}` has
// never emitted, so the first call announces immediately.
//
// ASSUMPTION (window edge): a call exactly kAnnounceThrottleMs after the
// last emission is allowed to announce again (>= comparison).
#include <cstdint>
#include <cstdio>
#include <optional>

#include "lists/collection_announce.h"

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

void case_first_call_announces() {
  lists::AnnounceState s;
  auto out = lists::MaybeAnnounce(s, 0, 10, 10000);
  check(out.has_value(), "first_announces");
  check(out->leadingIndex == 10, "first_latest_index");
  check(out->total == 10000, "first_total");
}

void case_throttle_suppresses_burst() {
  lists::AnnounceState s;
  auto first = lists::MaybeAnnounce(s, 1000, 0, 10000);
  check(first.has_value(), "burst_first_announces");
  bool all_suppressed = true;
  for (int i = 1; i <= 200; ++i) {
    auto out = lists::MaybeAnnounce(s, 1000 + i, i, 10000);
    if (out.has_value()) all_suppressed = false;
  }
  check(all_suppressed, "burst_suppressed_within_window");
}

void case_fling_collapses_to_one() {
  // A fast fling over hundreds of items emits O(1) announcements.
  lists::AnnounceState s;
  int emitted = 0;
  for (int i = 0; i < 500; ++i) {
    auto out = lists::MaybeAnnounce(s, i, i, 10000);  // 1ms apart
    if (out.has_value()) ++emitted;
  }
  check(emitted == 1, "fling_collapses_to_one");
}

void case_window_expiry_reannounces_latest() {
  lists::AnnounceState s;
  lists::MaybeAnnounce(s, 0, 5, 10000);
  auto held = lists::MaybeAnnounce(s, 100, 50, 10000);
  check(!held.has_value(), "within_window_suppressed");
  auto out = lists::MaybeAnnounce(s, lists::kAnnounceThrottleMs + 1, 120, 10000);
  check(out.has_value(), "after_window_announces");
  check(out->leadingIndex == 120, "after_window_latest_position");
}

void case_window_edge_announces() {
  lists::AnnounceState s;
  lists::MaybeAnnounce(s, 0, 0, 100);
  auto out = lists::MaybeAnnounce(s, lists::kAnnounceThrottleMs, 7, 100);
  check(out.has_value(), "edge_announces");
}

void case_same_index_still_throttled() {
  lists::AnnounceState s;
  lists::MaybeAnnounce(s, 0, 3, 100);
  auto out = lists::MaybeAnnounce(s, 10, 3, 100);
  check(!out.has_value(), "same_index_throttled");
}

void case_total_reported() {
  lists::AnnounceState s;
  auto out = lists::MaybeAnnounce(s, 0, 0, 42);
  check(out.has_value() && out->total == 42, "total_reported");
}

}  // namespace

int main() {
  case_first_call_announces();
  case_throttle_suppresses_burst();
  case_fling_collapses_to_one();
  case_window_expiry_reannounces_latest();
  case_window_edge_announces();
  case_same_index_still_throttled();
  case_total_reported();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
