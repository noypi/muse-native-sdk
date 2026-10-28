// T7 + T8(coalescing) contract tests — FR-LAYOUT-007 / AC-LAYOUT-014
// (TC-LAYOUT-014) + AC-LAYOUT-015 (TC-LAYOUT-015, coalescing section).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T7/T8 `Interface:` lines (reproduced verbatim in
// unit-tests/T7.md and unit-tests/T8.md).
//
// ASSUMPTION (node ids): `PropertyTree::set/get` take bare `NodeId`s; no
// tree construction is named in the interface, so these cases use small
// raw ids (1, 2, 3, ...) directly without a `Tree`/`Solver`.
// ASSUMPTION (handle stability): handles are content hashes (FNV-1a), so
// the same name yields the same handle across calls and translation units
// (pinned by the constexpr-vs-runtime case); distinct names yield distinct
// handles for the names used here (no collision asserted in general).
// ASSUMPTION (allocation shim): the counting `operator new` below is the
// "allocation-counter hook compiled into the test build" named in T7; the
// coder's production code needs no hook. Only `PropertyTree::set` runs
// while counting is armed, so any nonzero count is a per-mutation alloc.
// ASSUMPTION (concurrency): "data-race-free" is exercised with threads
// under ASan/TSan semantics — the test itself passes deterministically
// (each thread owns disjoint nodes); a race would surface as a failure
// under a sanitizer run, not as flakiness here.
// ASSUMPTION (coalescing granularity): each distinct node is its own
// subtree root for the one-node cases, so N sets on one node coalesce to
// exactly 1 invalidation; `invalidationCount()` deltas are measured per
// case because the counter is process-global.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <type_traits>
#include <vector>

#include "layout/batch.h"
#include "layout/property.h"
#include "layout/types.h"

// ---- Allocation-counter hook (test build only) ----
namespace {

std::atomic<bool> g_armAllocCount{false};
std::atomic<std::size_t> g_allocCount{0};

}  // namespace

void* operator new(std::size_t n) {
  if (g_armAllocCount.load(std::memory_order_relaxed))
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}

void* operator new[](std::size_t n) {
  if (g_armAllocCount.load(std::memory_order_relaxed))
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) return p;
  throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace {

// The mutation path is specified noexcept / lock-free: pin it.
static_assert(noexcept(layout::prop("width")),
              "prop must be noexcept");
static_assert(noexcept(layout::prop_constexpr("width")),
              "prop_constexpr must be noexcept");
static_assert(
    noexcept(std::declval<layout::PropertyTree&>().set(
        layout::NodeId{}, layout::PropHandle{}, 0.0f)),
    "PropertyTree::set must be noexcept");
static_assert(
    noexcept(std::declval<const layout::PropertyTree&>().get(
        layout::NodeId{}, layout::PropHandle{},
        std::declval<float&>())),
    "PropertyTree::get must be noexcept");

// ---- T7: handles section ----

// Named case (AC-LAYOUT-014): same name ⇒ same handle across calls.
static bool check_same_name_same_handle() {
  return layout::prop("width") == layout::prop("width") &&
         layout::prop("opacity") == layout::prop("opacity");
}

// Named case (AC-LAYOUT-014): distinct names ⇒ distinct handles.
static bool check_distinct_names_distinct_handles() {
  const layout::PropHandle w = layout::prop("width");
  const layout::PropHandle h = layout::prop("height");
  const layout::PropHandle o = layout::prop("opacity");
  return w != h && w != o && h != o;
}

// Named case (T7 interface): literals hash at compile time — the
// constexpr handle equals the runtime handle for the same name.
static bool check_constexpr_matches_runtime() {
  constexpr layout::PropHandle kW = layout::prop_constexpr("width");
  constexpr layout::PropHandle kO = layout::prop_constexpr("opacity");
  static_assert(kW != kO, "distinct literals must hash distinctly");
  return kW == layout::prop("width") && kO == layout::prop("opacity");
}

// Named case (AC-LAYOUT-014): set→get round-trip on one node.
static bool check_set_get_round_trip() {
  layout::PropertyTree tree;
  const layout::NodeId node = 1;
  const layout::PropHandle w = layout::prop("width");
  const layout::PropHandle o = layout::prop("opacity");
  tree.set(node, w, 120.0f);
  tree.set(node, o, 0.5f);
  float out = 0.0f;
  if (!tree.get(node, w, out) || out != 120.0f) return false;
  if (!tree.get(node, o, out) || out != 0.5f) return false;
  // Overwrite replaces the value.
  tree.set(node, w, 200.0f);
  return tree.get(node, w, out) && out == 200.0f;
}

// Named case (T7 interface): unknown handle on get returns false.
static bool check_unknown_handle_get_false() {
  layout::PropertyTree tree;
  tree.set(1, layout::prop("width"), 10.0f);
  float out = 0.0f;
  // Never-set handle on a known node.
  if (tree.get(1, layout::prop("height"), out)) return false;
  // Known handle on a never-touched node.
  return !tree.get(2, layout::prop("width"), out);
}

// Named case (AC-LAYOUT-014): per-mutation allocation counter is 0 over
// 1k mutations (counting shim armed only around the set loop).
static bool check_no_allocation_over_1k_mutations() {
  layout::PropertyTree tree;
  const layout::PropHandle w = layout::prop("width");
  // Warm up outside the counting window (table/node provisioning).
  for (std::uint32_t i = 1; i <= 8; ++i) tree.set(i, w, 1.0f);
  g_allocCount.store(0, std::memory_order_relaxed);
  g_armAllocCount.store(true, std::memory_order_relaxed);
  for (std::uint32_t i = 0; i < 1000; ++i)
    tree.set(static_cast<layout::NodeId>(1 + (i % 8)), w,
             static_cast<float>(i));
  g_armAllocCount.store(false, std::memory_order_relaxed);
  if (g_allocCount.load(std::memory_order_relaxed) != 0) return false;
  // Values still land after the counted window.
  float out = 0.0f;
  return tree.get(1, w, out);
}

// Named case (T7 detail): concurrent set on distinct nodes is
// data-race-free; every thread's values round-trip afterwards.
static bool check_concurrent_set_distinct_nodes_race_free() {
  layout::PropertyTree tree;
  const layout::PropHandle w = layout::prop("width");
  constexpr int kThreads = 8;
  constexpr int kPerThread = 100;
  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t]() {
      const layout::NodeId node = static_cast<layout::NodeId>(100 + t);
      for (int i = 0; i < kPerThread; ++i)
        tree.set(node, w, static_cast<float>(t * 1000 + i));
    });
  }
  for (auto& th : threads) th.join();
  for (int t = 0; t < kThreads; ++t) {
    float out = 0.0f;
    const layout::NodeId node = static_cast<layout::NodeId>(100 + t);
    if (!tree.get(node, w, out)) return false;
    if (out != static_cast<float>(t * 1000 + kPerThread - 1)) return false;
  }
  return true;
}

// ---- T8: coalescing section (style_mutation side, TC-LAYOUT-015) ----

class CountingSink : public layout::InvalidationSink {
 public:
  void onInvalidated(layout::NodeId subtreeRoot) override {
    ++calls;
    lastRoot = subtreeRoot;
    roots.push_back(subtreeRoot);
  }
  int calls = 0;
  layout::NodeId lastRoot = layout::kInvalidNodeId;
  std::vector<layout::NodeId> roots;
};

struct SinkGuard {
  explicit SinkGuard(layout::InvalidationSink* s) {
    layout::setInvalidationSink(s);
  }
  ~SinkGuard() { layout::setInvalidationSink(nullptr); }
};

// Named case (T8 interface): outside a BatchScope, setBatched invalidates
// immediately (exactly one invalidation for one call).
static bool check_set_batched_outside_scope_invalidates_immediately() {
  layout::PropertyTree tree;
  CountingSink sink;
  SinkGuard guard(&sink);
  const std::uint64_t base = layout::invalidationCount();
  layout::setBatched(tree, 1, layout::prop("width"), 10.0f);
  return layout::invalidationCount() == base + 1 && sink.calls == 1;
}

// Named case (AC-LAYOUT-015): N=50 sequential batched mutations on one
// node ⇒ invalidationCount delta 1 inside one scope.
static bool check_batch_50_sets_one_node_delta_1() {
  layout::PropertyTree tree;
  CountingSink sink;
  SinkGuard guard(&sink);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scope(tree);
    for (int i = 0; i < 50; ++i)
      layout::setBatched(tree, 1, w, static_cast<float>(i));
  }
  return layout::invalidationCount() == base + 1 && sink.calls == 1;
}

// Named case (T8 interface): nested scopes coalesce to the outermost
// close — one invalidation total, none delivered while inner scopes
// are still open.
static bool check_nested_scopes_coalesce_to_outermost_close() {
  layout::PropertyTree tree;
  CountingSink sink;
  SinkGuard guard(&sink);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope outer(tree);
    layout::setBatched(tree, 1, w, 1.0f);
    {
      layout::BatchScope inner(tree);
      layout::setBatched(tree, 1, w, 2.0f);
      if (layout::invalidationCount() != base) return false;
    }
    // Inner closed, outer still open: still coalesced.
    if (layout::invalidationCount() != base) return false;
    layout::setBatched(tree, 1, w, 3.0f);
  }
  return layout::invalidationCount() == base + 1 && sink.calls == 1;
}

// Named case (T8 interface): nullptr sink restores the counting default
// — batching still coalesces to delta 1 with no observer installed.
static bool check_nullptr_sink_restores_counting_default() {
  layout::PropertyTree tree;
  CountingSink sink;
  layout::setInvalidationSink(&sink);
  layout::setInvalidationSink(nullptr);
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scope(tree);
    for (int i = 0; i < 10; ++i)
      layout::setBatched(tree, 1, layout::prop("width"),
                          static_cast<float>(i));
  }
  return layout::invalidationCount() == base + 1;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  // Handles section (T7 / TC-LAYOUT-014).
  RUN_CASE(check_same_name_same_handle);
  RUN_CASE(check_distinct_names_distinct_handles);
  RUN_CASE(check_constexpr_matches_runtime);
  RUN_CASE(check_set_get_round_trip);
  RUN_CASE(check_unknown_handle_get_false);
  RUN_CASE(check_no_allocation_over_1k_mutations);
  RUN_CASE(check_concurrent_set_distinct_nodes_race_free);
  // Coalescing section (T8 / TC-LAYOUT-015).
  RUN_CASE(check_set_batched_outside_scope_invalidates_immediately);
  RUN_CASE(check_batch_50_sets_one_node_delta_1);
  RUN_CASE(check_nested_scopes_coalesce_to_outermost_close);
  RUN_CASE(check_nullptr_sink_restores_counting_default);
  if (failures > 0) {
    std::printf("style_mutation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("style_mutation_test: all passing\n");
  return 0;
}
