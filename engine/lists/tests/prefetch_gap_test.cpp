// T3 prefetch/gap-worker tests — bug-m3-fidelity-009.
// Binds to the plan T3 `Interface:` lines:
//   `PrefetchPlan planPrefetch(float velocityPxS, int frameBudgetNs,
//                              int viewportRows) -> PrefetchPlan
//     { int distanceRows; int bindBudgetNs; }`
//   + `GapWorker::submit(PrefetchPlan) -> Status`
//   / `GapWorker::takeReady() -> Holder`
// Hermetic: C++17 + lists headers only; the worker's own std::jthread is the
// only threading (no device, no Choreographer — deadlines/VsyncDeadline are
// never touched here); no network, no env/config reads, no filesystem, no
// live services, no wall-clock sleeps (bounded yield-spins only).
// Convention copied from physics_integrate_test.cpp: anonymous namespace,
// static bool check_* cases, printf PASS/FAIL, non-zero exit on failure.
// Include path follows the engine convention:
//   #include "lists/prefetch_gap.h" with engine/lists/include on the path.
//
// ASSUMPTIONS (spelling proposals — the plan Interface: line names the
// entry points but not their member spellings; if the coder spells members
// differently, these call sites move — never the expected behavior):
//   A1. `lists::PrefetchPlan{.distanceRows, .bindBudgetNs}` (ints, rows/ns).
//   A2. `lists::Status::{kOk, kInvalidArg}` for GapWorker::submit.
//   A3. `lists::Holder` with `explicit operator bool` (valid == ready to
//       bind); unentered holders return to the pool on destruction (RAII),
//       so dropping a Holder without entering it costs nothing visible.
//   A4. `lists::GapWorker` is default-constructible (starts its core-pool
//       worker), non-copyable, joins on destruction; `submit` is callable
//       from any thread (worker-thread dispatch is internal); `takeReady`
//       never blocks the caller — it returns an invalid Holder when nothing
//       is ready yet, so the UI thread polls without stalling.
//   A5. Rest (velocity 0) prefetches nothing: distanceRows == 0. Any
//       rest-prefetch the coder adds breaks this case by design choice;
//       reconcile via contract, not by weakening.
//   A6. Reverse-fling magnitude symmetry: d(-v) == d(v) (direction selects
//       which side, distance is speed-derived).
//   A7. Over-budget binds defer, never drop: submit returns kOk and a later
//       takeReady still yields the holder within a bounded spin.
//   A8. Fail-closed invalid inputs (non-positive frameBudgetNs, empty or
//       negative viewportRows, non-finite velocity) yield distanceRows == 0
//       and bindBudgetNs == 0 without throwing.
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <vector>

#include "lists/prefetch_gap.h"

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

constexpr int kFrameBudgetNs60 = 16666667;
constexpr int kViewportRows = 10;

// Bounded poll: the worker's jthread resolves quickly; never sleep, never
// read the wall clock — yield only, with a hard iteration cap so a stuck
// worker fails the case instead of hanging the suite.
lists::Holder poll_ready(lists::GapWorker& worker) {
  for (int i = 0; i < 200000; ++i) {
    lists::Holder h = worker.takeReady();
    if (static_cast<bool>(h)) return h;
    std::this_thread::yield();
  }
  return lists::Holder{};
}

void case_rest_plan_is_empty() {
  lists::PrefetchPlan p = lists::planPrefetch(0.0f, kFrameBudgetNs60, kViewportRows);
  check(p.distanceRows == 0 && p.bindBudgetNs >= 0 &&
            p.bindBudgetNs <= kFrameBudgetNs60,
        "rest_plan_is_empty");
}

void case_fling_extends_distance() {
  lists::PrefetchPlan rest =
      lists::planPrefetch(0.0f, kFrameBudgetNs60, kViewportRows);
  lists::PrefetchPlan fling =
      lists::planPrefetch(8000.0f, kFrameBudgetNs60, kViewportRows);
  check(fling.distanceRows > rest.distanceRows && fling.bindBudgetNs >= 0 &&
            fling.bindBudgetNs <= kFrameBudgetNs60,
        "fling_extends_distance");
}

void case_reverse_fling_mirrors_forward() {
  lists::PrefetchPlan fwd =
      lists::planPrefetch(8000.0f, kFrameBudgetNs60, kViewportRows);
  lists::PrefetchPlan rev =
      lists::planPrefetch(-8000.0f, kFrameBudgetNs60, kViewportRows);
  check(rev.distanceRows >= 0 && rev.distanceRows == fwd.distanceRows &&
            rev.bindBudgetNs >= 0 && rev.bindBudgetNs <= kFrameBudgetNs60,
        "reverse_fling_mirrors_forward");
}

void case_frame_budget_caps_bind() {
  lists::PrefetchPlan huge =
      lists::planPrefetch(1.0e9f, kFrameBudgetNs60, kViewportRows);
  check(huge.distanceRows >= 0 && huge.bindBudgetNs >= 0 &&
            huge.bindBudgetNs <= kFrameBudgetNs60,
        "frame_budget_caps_bind");
  lists::PrefetchPlan small_budget =
      lists::planPrefetch(8000.0f, 8333333, kViewportRows);
  lists::PrefetchPlan large_budget =
      lists::planPrefetch(8000.0f, kFrameBudgetNs60, kViewportRows);
  check(small_budget.bindBudgetNs >= 0 &&
            small_budget.bindBudgetNs <= large_budget.bindBudgetNs,
        "bind_budget_scales_with_frame_budget");
}

void case_invalid_inputs_fail_closed() {
  bool ok = true;
  try {
    lists::PrefetchPlan no_budget =
        lists::planPrefetch(8000.0f, 0, kViewportRows);
    ok = ok && (no_budget.distanceRows == 0 && no_budget.bindBudgetNs == 0);
    lists::PrefetchPlan no_rows =
        lists::planPrefetch(8000.0f, kFrameBudgetNs60, 0);
    ok = ok && (no_rows.distanceRows == 0 && no_rows.bindBudgetNs == 0);
    lists::PrefetchPlan neg_rows =
        lists::planPrefetch(8000.0f, kFrameBudgetNs60, -4);
    ok = ok && (neg_rows.distanceRows == 0 && neg_rows.bindBudgetNs == 0);
    lists::PrefetchPlan neg_budget =
        lists::planPrefetch(8000.0f, -100, kViewportRows);
    ok = ok && (neg_budget.distanceRows == 0 && neg_budget.bindBudgetNs == 0);
  } catch (...) {
    ok = false;
  }
  check(ok, "invalid_inputs_fail_closed");
}

void case_extreme_velocity_never_throws() {
  bool ok = true;
  try {
    lists::PrefetchPlan inf =
        lists::planPrefetch(std::numeric_limits<float>::infinity(),
                            kFrameBudgetNs60, kViewportRows);
    ok = ok && (inf.distanceRows == 0);
    lists::PrefetchPlan nan =
        lists::planPrefetch(std::numeric_limits<float>::quiet_NaN(),
                            kFrameBudgetNs60, kViewportRows);
    ok = ok && (nan.distanceRows == 0);
    lists::PrefetchPlan huge =
        lists::planPrefetch(1.0e30f, kFrameBudgetNs60, kViewportRows);
    ok = ok && (huge.distanceRows >= 0 &&
                huge.bindBudgetNs <= kFrameBudgetNs60);
  } catch (...) {
    ok = false;
  }
  check(ok, "extreme_velocity_never_throws");
}

void case_submit_take_ready_delivers_holder() {
  lists::GapWorker worker;
  lists::PrefetchPlan p =
      lists::planPrefetch(8000.0f, kFrameBudgetNs60, kViewportRows);
  bool ok = (worker.submit(p) == lists::Status::kOk);
  lists::Holder h = poll_ready(worker);
  ok = ok && static_cast<bool>(h);
  check(ok, "submit_take_ready_delivers_holder");
}

void case_unentered_holders_return_to_pool() {
  lists::GapWorker worker;
  lists::PrefetchPlan p =
      lists::planPrefetch(8000.0f, kFrameBudgetNs60, kViewportRows);
  bool ok = true;
  // Take 32 holders and drop every one unentered: the pool must keep
  // serving with zero visible cost (every submit kOk, every take valid) —
  // no leak, no exhaustion, no blank.
  for (int i = 0; i < 32; ++i) {
    if (worker.submit(p) != lists::Status::kOk) {
      ok = false;
      break;
    }
    lists::Holder h = poll_ready(worker);
    if (!static_cast<bool>(h)) {
      ok = false;
      break;
    }
    // Unentered: destroyed here, must return to the pool.
  }
  check(ok, "unentered_holders_return_to_pool");
}

void case_over_budget_bind_defers_never_drops() {
  lists::GapWorker worker;
  // A 1ns bind budget cannot fit any bind: the worker defers the work and
  // still delivers the holder (defer != drop, never blank).
  lists::PrefetchPlan p =
      lists::planPrefetch(8000.0f, kFrameBudgetNs60, kViewportRows);
  p.bindBudgetNs = 1;
  bool ok = (worker.submit(p) == lists::Status::kOk);
  lists::Holder h = poll_ready(worker);
  ok = ok && static_cast<bool>(h);
  // The pool stays usable after a deferred bind.
  if (ok) {
    ok = (worker.submit(p) == lists::Status::kOk);
    lists::Holder h2 = poll_ready(worker);
    ok = ok && static_cast<bool>(h2);
  }
  check(ok, "over_budget_bind_defers_never_drops");
}

void case_invalid_submit_rejected_take_empty_is_invalid() {
  lists::GapWorker worker;
  lists::PrefetchPlan bad{};
  bad.distanceRows = -3;
  bad.bindBudgetNs = 100;
  bool ok = (worker.submit(bad) == lists::Status::kInvalidArg);
  // Nothing pending: takeReady reports empty immediately, never blocks.
  lists::Holder h = worker.takeReady();
  ok = ok && !static_cast<bool>(h);
  check(ok, "invalid_submit_rejected_take_empty_is_invalid");
}

}  // namespace

int main() {
  case_rest_plan_is_empty();
  case_fling_extends_distance();
  case_reverse_fling_mirrors_forward();
  case_frame_budget_caps_bind();
  case_invalid_inputs_fail_closed();
  case_extreme_velocity_never_throws();
  case_submit_take_ready_delivers_holder();
  case_unentered_holders_return_to_pool();
  case_over_budget_bind_defers_never_drops();
  case_invalid_submit_rejected_take_empty_is_invalid();
  if (g_failures > 0) {
    std::printf("prefetch_gap_test: %d failing\n", g_failures);
    return 1;
  }
  std::printf("prefetch_gap_test: all passing\n");
  return 0;
}
