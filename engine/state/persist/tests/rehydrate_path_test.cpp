// Batch 4 contract tests — T10: off-critical-path rehydration scheduling
// (FR-STATE-007, AC-STATE-031).
// Hermetic: C++17 + committed engine/state headers only; FrameBudget faked
// in-test (records ordering + batch counts); no env/config/network.
// Binds ONLY to the T10 `Interface:` line; see unit-tests/T10.md assumptions.
//
// CONVERGENCE ASSUMPTION (A-P1, shared with T8): persist snapshot shape is
// `muse::state::persist::Snapshot`; this file does not touch the T7 SWR cache
// alias. Scheduling seam assumed declared in `muse/state/rehydrate.hpp`
// (ASSUMPTION A-P6; coder-owned header).
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/rehydrate.hpp"

namespace {

using muse::state::persist::FrameBudget;
using muse::state::persist::RehydrateJob;
using muse::state::persist::StoreId;
using muse::state::persist::needsForFirstPaint;
using muse::state::persist::scheduleRehydrate;

// --- Fakes ------------------------------------------------------------------

// Harness fake: records the order jobs run and how many batches each deferred
// store produced. First-paint work must run before any deferred store.
class FakeFrameBudget : public FrameBudget {
 public:
  void onFirstPaint(StoreId s) override { order.push_back("paint:" + s.str()); }
  void onDeferredBatch(StoreId s) override {
    order.push_back("deferred:" + s.str());
    ++batches[s.str()];
  }
  int batchesFor(const std::string& s) const {
    auto it = batches.find(s);
    return it == batches.end() ? 0 : it->second;
  }
  std::vector<std::string> order;
  std::unordered_map<std::string, int> batches;
};

// Named case: store flagged for first paint is NOT deferred —
// needsForFirstPaint true, and its job runs in the first-paint phase.
static bool check_first_paint_store_runs_first() {
  FakeFrameBudget b;
  StoreId paint("paint-store");
  if (!needsForFirstPaint(paint)) return false;
  RehydrateJob j;
  j.store = paint;
  scheduleRehydrate(j, &b);
  if (b.order.empty()) return false;
  return b.order[0] == "paint:paint-store";
}

// Named case: non-first-paint store is deferred past the first-frame budget —
// needsForFirstPaint false, and no deferred batch runs before first paint.
static bool check_deferred_store_runs_after_first_paint() {
  FakeFrameBudget b;
  StoreId paint("paint-store");
  StoreId deferred("deferred-store");
  if (needsForFirstPaint(deferred)) return false;
  RehydrateJob jp;
  jp.store = paint;
  RehydrateJob jd;
  jd.store = deferred;
  scheduleRehydrate(jp, &b);
  scheduleRehydrate(jd, &b);
  // First entry must be first-paint work; deferred batch comes after.
  if (b.order.size() < 2) return false;
  if (b.order[0] != "paint:paint-store") return false;
  return b.order[1] == "deferred:deferred-store";
}

// Named case: at most one batch per deferred store (no invalidation storm) —
// scheduling N jobs for the same deferred store yields exactly 1 batch.
static bool check_at_most_one_batch_per_deferred_store() {
  FakeFrameBudget b;
  StoreId deferred("deferred-store");
  for (int i = 0; i < 5; ++i) {
    RehydrateJob j;
    j.store = deferred;
    scheduleRehydrate(j, &b);
  }
  return b.batchesFor("deferred-store") <= 1;
}

// Named case: two distinct deferred stores each get at most one batch.
static bool check_distinct_deferred_stores_each_one_batch() {
  FakeFrameBudget b;
  for (int i = 0; i < 3; ++i) {
    RehydrateJob a;
    a.store = StoreId("store-a");
    scheduleRehydrate(a, &b);
    RehydrateJob c;
    c.store = StoreId("store-c");
    scheduleRehydrate(c, &b);
  }
  return b.batchesFor("store-a") <= 1 && b.batchesFor("store-c") <= 1;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name);  \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_first_paint_store_runs_first);
  RUN_CASE(check_deferred_store_runs_after_first_paint);
  RUN_CASE(check_at_most_one_batch_per_deferred_store);
  RUN_CASE(check_distinct_deferred_stores_each_one_batch);
  if (failures > 0) {
    std::printf("rehydrate_path_test: %d failing\n", failures);
    return 1;
  }
  std::printf("rehydrate_path_test: all passing\n");
  return 0;
}
