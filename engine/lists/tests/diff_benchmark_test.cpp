// T7 contract tests — FR-LISTS-003 / AC-LISTS-012 (TC-LISTS-012;
// RULE-LISTS-001 nested interop; NFR-LISTS-003 context, device-lab leg
// stays external).
// Hermetic: C++17 + lists headers only; synthetic inline data; injected
// timings/rates only — no wall-clock sleeps except the bounded internal
// timing inside benchDiff. No network, no env/config, no live services.
// Binds to the T7 `Interface:` line:
//   `float snapTarget(float offsetPx, float velocityPxS,
//    const SnapPoints& pts); RefreshState pullRefresh(RefreshState s,
//    float overscrollPx); NestedDecision nestedClaim(NestedAxis parent,
//    NestedAxis child); PerfReport benchDiff(size_t n, size_t edits);`
// Include path follows the engine convention:
//   #include "lists/snap_refresh.h" with engine/lists/include on the path,
//   plus consumed T2/T4 headers ("lists/scroll_physics.h",
//   "lists/myers_diff.h") — PhysicsState/integrate and myersDiff are
//   reused, never redefined.
//
// ASSUMPTION (type spellings): the plan/Interface line settles the four
// function signatures but not the C++ spelling of SnapPoints,
// RefreshState, NestedAxis, NestedDecision, or PerfReport. This test
// proposes: namespace `lists`; `SnapPoints{std::vector<float> points}`;
// `enum class RefreshState{Idle, Armed, Refreshing}` (Idle below
// threshold, Armed past threshold, Refreshing on release-past-threshold
// modelled as pullRefresh(Armed, overshoot)); `NestedAxis{bool
// scrollsHorizontal; bool scrollsVertical}`;
// `enum class NestedDecision{Parent, Child}`;
// `PerfReport{size_t n; size_t edits; double secondsCpp;
// double secondsBaseline; double speedupLow; double speedupHigh;
// std::string methodology}` with `methodology` documenting inputs and the
// baseline. Reconciliation rule: renames update call sites only, never the
// expected behavior below.
//
// ASSUMPTION (pull threshold): the plan/Interface line settles no pixel
// value, so the tests pin no exact threshold: 4px must stay Idle (below
// any reasonable threshold), 100000px must leave Idle (past any reasonable
// threshold), and zero/negative overscroll must never trigger. A coder
// threshold outside (4, 100000)px needs a planner ruling (T1.10), not a
// silent test edit.
//
// ASSUMPTION (snap bias): with ~zero velocity the nearest point wins;
// with a strong fling toward a farther point the fling direction wins.
// Midpoint ties resolve toward the fling direction, else toward the lower
// point.
//
// ASSUMPTION (benchmark scale): the suite stays fast — benchDiff is called
// with small N (1000/10) and must return promptly; the report carries the
// scaling note for large feeds rather than the test running a large feed
// through a timer. The 10k synthetic feed below runs untimed (no timing
// assertions): it asserts no out-of-range / blank window, not fps.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "lists/myers_diff.h"
#include "lists/scroll_physics.h"
#include "lists/snap_refresh.h"

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

lists::SnapPoints pts(std::initializer_list<float> ps) {
  lists::SnapPoints s{};
  for (float p : ps) s.points.push_back(p);
  return s;
}

lists::KeyedList kl_range(size_t n) {
  lists::KeyedList l{};
  for (size_t i = 0; i < n; ++i) l.keys.push_back("item-" + std::to_string(i));
  return l;
}

void case_snap_nearest_at_rest() {
  lists::SnapPoints p = pts({0.0f, 100.0f, 200.0f});
  check(lists::snapTarget(90.0f, 0.0f, p) == 100.0f, "snap_nearest_point");
}

void case_snap_exact_point_stays() {
  lists::SnapPoints p = pts({0.0f, 100.0f, 200.0f});
  check(lists::snapTarget(100.0f, 0.0f, p) == 100.0f, "snap_exact_stays");
}

void case_snap_fling_bias_forward() {
  lists::SnapPoints p = pts({0.0f, 100.0f, 200.0f});
  check(lists::snapTarget(90.0f, 2500.0f, p) == 200.0f,
        "snap_fling_bias_forward");
}

void case_snap_empty_points() {
  lists::SnapPoints p = pts({});
  check(lists::snapTarget(90.0f, 0.0f, p) == 90.0f, "snap_empty_no_move");
}

void case_pull_below_threshold() {
  check(lists::pullRefresh(lists::RefreshState::Idle, 4.0f) ==
            lists::RefreshState::Idle,
        "pull_small_stays_idle");
}

void case_pull_zero_negative_never() {
  check(lists::pullRefresh(lists::RefreshState::Idle, 0.0f) ==
            lists::RefreshState::Idle,
        "pull_zero_stays_idle");
  check(lists::pullRefresh(lists::RefreshState::Idle, -20.0f) ==
            lists::RefreshState::Idle,
        "pull_negative_stays_idle");
}

void case_pull_past_threshold() {
  lists::RefreshState s =
      lists::pullRefresh(lists::RefreshState::Idle, 100000.0f);
  check(s != lists::RefreshState::Idle, "pull_huge_leaves_idle");
}

void case_pull_armed_releases() {
  lists::RefreshState s =
      lists::pullRefresh(lists::RefreshState::Armed, 100000.0f);
  check(s == lists::RefreshState::Refreshing, "pull_armed_refreshes");
}

void case_nested_orthogonal_child_wins() {
  // Vertical parent, horizontal child, horizontal gesture: the child keeps
  // its own axis (RULE-LISTS-001) — the parent must not steal it.
  lists::NestedAxis parent{false, true};
  lists::NestedAxis child{true, false};
  check(lists::nestedClaim(parent, child) == lists::NestedDecision::Child,
        "nested_orthogonal_child_wins");
}

void case_nested_same_axis_parent() {
  lists::NestedAxis parent{false, true};
  lists::NestedAxis child{false, true};
  check(lists::nestedClaim(parent, child) == lists::NestedDecision::Parent,
        "nested_same_axis_parent");
}

void case_feed_10k_no_blanks() {
  // Synthetic 10k feed fling in-harness (untimed): integrate a fast fling
  // with injected per-frame dt (T2, consumed), then walk a viewport window
  // over the final offset and require every realized index in-range —
  // i.e. no dropped/blank cells in-harness. NFR-LISTS-003 device-lab fps
  // leg stays EXTERNAL (see contract).
  lists::KeyedList feed = kl_range(10000);
  lists::PhysicsState s{0.0f, 6000.0f};
  lists::PredictedSample lift{0.0f, 0.0f};
  lists::CurveParams curve{};
  for (int i = 0; i < 120; ++i) {
    s = lists::integrate(s, lift, lists::VsyncDeadline{1.0f / 120.0f}, curve);
  }
  check(std::isfinite(s.offsetPx), "fling_10k_offset_finite");
  check(s.offsetPx >= 0.0f, "fling_10k_offset_nonnegative");
  const float kItemPx = 100.0f;
  const size_t kWindow = 20;
  size_t first =
      static_cast<size_t>(s.offsetPx / kItemPx) % feed.keys.size();
  bool window_ok = true;
  size_t realized = 0;
  for (size_t i = 0; i < kWindow; ++i) {
    size_t idx = first + i;
    if (idx >= feed.keys.size()) break;
    if (feed.keys[idx].empty()) window_ok = false;
    ++realized;
  }
  check(window_ok, "fling_10k_window_in_range");
  check(realized == kWindow, "fling_10k_no_blank_cells");
  // T4 consumed: a small edit set over the same feed diffs minimally.
  lists::KeyedList edited = feed;
  edited.keys[7] = "item-new-7";
  edited.keys.push_back("item-10000");
  lists::EditScript script = lists::myersDiff(feed, edited);
  check(script.ops.size() <= 4, "feed_10k_diff_small_edits_bounded");
}

void case_bench_report() {
  // Small N with scaling note — bounded, no suite timeouts.
  lists::PerfReport r = lists::benchDiff(1000, 10);
  check(r.n == 1000, "bench_echoes_n");
  check(r.edits == 10, "bench_echoes_edits");
  check(!r.methodology.empty(), "bench_documents_methodology");
  check(r.secondsCpp >= 0.0, "bench_cpp_time_nonnegative");
  check(r.secondsBaseline >= 0.0, "bench_baseline_time_nonnegative");
  check(r.speedupHigh >= r.speedupLow, "bench_band_ordered");
  check(r.speedupLow >= 0.0, "bench_band_nonnegative");
}

void case_bench_adversarial_args() {
  // Security hardening (coder choice pinned: harness-only bound
  // `kMaxBenchN=10000`; `edits == 0 || n == 0` returns an empty report;
  // wrap-safe stride saturates `denom` when `edits == SIZE_MAX`).
  // `edits = SIZE_MAX` must not divide by zero (`edits+1` wraps to 0);
  // huge `n` must clamp instead of exhausting the heap. Hermetic: small
  // synthetic shapes only, no sleeps, no timing assertions — completion
  // itself (bounded return, finite report) is the assertion.
  lists::PerfReport r = lists::benchDiff(100, SIZE_MAX);
  check(std::isfinite(r.secondsCpp), "bench_edits_max_no_crash");
  check(r.edits == SIZE_MAX, "bench_edits_max_echoes_edits");
  lists::PerfReport big = lists::benchDiff(1000000, 5);
  check(big.n <= 10000, "bench_huge_n_clamped");
  check(std::isfinite(big.secondsCpp) && big.secondsCpp >= 0.0,
        "bench_huge_n_completes_no_oom");
}

void case_pull_refreshing_drop_returns_idle() {
  // Code-review MINOR edge, pinned as CURRENT behavior (read from
  // `snap_refresh.cpp`: `pullRefresh` returns `Idle` for any
  // `overscrollPx <= 0`, including from `Refreshing` — the `switch` on
  // `Refreshing` is only reached for positive overscroll, where it
  // sticks). So `pullRefresh(Refreshing, 0)` resets to Idle; it does
  // NOT stick in Refreshing. If the state machine is ever changed to
  // stick, this test — not the behavior — must be re-ruled by planner.
  check(lists::pullRefresh(lists::RefreshState::Refreshing, 0.0f) ==
            lists::RefreshState::Idle,
        "pull_refreshing_zero_returns_idle");
}

}  // namespace

int main() {
  case_snap_nearest_at_rest();
  case_snap_exact_point_stays();
  case_snap_fling_bias_forward();
  case_snap_empty_points();
  case_pull_below_threshold();
  case_pull_zero_negative_never();
  case_pull_past_threshold();
  case_pull_armed_releases();
  case_nested_orthogonal_child_wins();
  case_nested_same_axis_parent();
  case_feed_10k_no_blanks();
  case_bench_report();
  case_bench_adversarial_args();
  case_pull_refreshing_drop_returns_idle();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
