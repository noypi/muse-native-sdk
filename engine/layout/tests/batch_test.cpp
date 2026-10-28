// T8 contract tests — FR-LAYOUT-007 / AC-LAYOUT-016 (TC-LAYOUT-016).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T8 `Interface:` line (reproduced verbatim in
// unit-tests/T8.md).
//
// ASSUMPTION (subtree identity): the interface names "one invalidation
// per affected subtree-root" without fixing how nodes map to subtrees.
// These cases use distinct top-level node ids (1, 2, 3) as distinct
// subtrees — the minimal reading consistent with the one-node ⇒ delta 1
// case. A coder mapping several nodes under one subtree root would
// legitimately report fewer invalidations; the one-node and no-alloc
// cases pin the behavior, not the subtree partition.
// ASSUMPTION (descriptor update): "one descriptor update" in AC-LAYOUT-016
// is observed as exactly one `InvalidationSink::onInvalidated` delivery
// for the 50×-one-node batch — the sink is the only observer surface in
// the interface.
// ASSUMPTION (allocation shim): as in style_mutation_test.cpp, the
// counting `operator new` below is the test-build allocation hook; only
// `setBatched` inside a scope runs while counting is armed.
// ASSUMPTION (pendingCount): asserted only for the fresh-scope zero it
// trivially pins (no queued work yet) plus a nonzero-while-dirty check;
// exact queued-entry encoding is coder-owned.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

static_assert(noexcept(layout::invalidationCount()),
              "invalidationCount must be noexcept");
static_assert(noexcept(layout::setInvalidationSink(nullptr)),
              "setInvalidationSink must be noexcept");

class CountingSink : public layout::InvalidationSink {
 public:
  void onInvalidated(layout::NodeId subtreeRoot) override {
    ++calls;
    roots.push_back(subtreeRoot);
  }
  int calls = 0;
  std::vector<layout::NodeId> roots;
};

struct SinkGuard {
  explicit SinkGuard(layout::InvalidationSink* s) {
    layout::setInvalidationSink(s);
  }
  ~SinkGuard() { layout::setInvalidationSink(nullptr); }
};

// Named case (AC-LAYOUT-016): 50× setBatched on one node in one scope ⇒
// invalidationCount delta 1 AND exactly 1 descriptor update (1 sink call).
static bool check_50_sets_one_node_delta_1_single_descriptor() {
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
  if (layout::invalidationCount() != base + 1) return false;
  if (sink.calls != 1) return false;
  // The single delivery names the touched subtree.
  return sink.roots.size() == 1 && sink.roots[0] == 1;
}

// Named case (T8 detail): one batch touching 3 distinct subtrees ⇒
// delta 3, one invalidation per touched subtree.
static bool check_3_subtree_batch_delta_3() {
  layout::PropertyTree tree;
  CountingSink sink;
  SinkGuard guard(&sink);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scope(tree);
    layout::setBatched(tree, 1, w, 1.0f);
    layout::setBatched(tree, 2, w, 2.0f);
    layout::setBatched(tree, 3, w, 3.0f);
  }
  if (layout::invalidationCount() != base + 3) return false;
  return sink.calls == 3;
}

// Named case (AC-LAYOUT-016): no per-mutation allocation across a 50×
// batched run (counting shim armed only around the batch body).
static bool check_batched_no_per_mutation_allocation() {
  layout::PropertyTree tree;
  CountingSink sink;
  SinkGuard guard(&sink);
  const layout::PropHandle w = layout::prop("width");
  // Warm up outside the counting window. Pre-reserve the sink's storage and
  // touch ≥2 distinct subtrees so the counted window's single delivery needs
  // no vector growth (otherwise the sink's own realloc would be miscounted
  // as a production allocation).
  sink.roots.reserve(64);
  {
    layout::BatchScope warm(tree);
    for (int i = 0; i < 5; ++i)
      layout::setBatched(tree, 1, w, static_cast<float>(i));
    layout::setBatched(tree, 2, w, 42.0f);
  }
  g_allocCount.store(0, std::memory_order_relaxed);
  g_armAllocCount.store(true, std::memory_order_relaxed);
  {
    layout::BatchScope scope(tree);
    for (int i = 0; i < 50; ++i)
      layout::setBatched(tree, 1, w, static_cast<float>(i));
  }
  g_armAllocCount.store(false, std::memory_order_relaxed);
  return g_allocCount.load(std::memory_order_relaxed) == 0;
}

// Named case (T8 interface): fresh scope reports zero pending; queued
// work shows nonzero pending while the scope is open.
static bool check_pending_count_tracks_queued_work() {
  layout::PropertyTree tree;
  SinkGuard guard(nullptr);
  layout::BatchScope scope(tree);
  if (scope.pendingCount() != 0) return false;
  layout::setBatched(tree, 1, layout::prop("width"), 1.0f);
  return scope.pendingCount() > 0;
}

// Named case (T8 interface): nullptr sink restores the counting default
// — coalescing still yields delta 1 with no observer installed, and a
// later stub receives new deliveries (global state is not stuck).
static bool check_nullptr_sink_restores_counting_default() {
  layout::PropertyTree tree;
  const layout::PropHandle w = layout::prop("width");
  {
    CountingSink first;
    layout::setInvalidationSink(&first);
  }
  layout::setInvalidationSink(nullptr);
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scope(tree);
    for (int i = 0; i < 10; ++i)
      layout::setBatched(tree, 1, w, static_cast<float>(i));
  }
  if (layout::invalidationCount() != base + 1) return false;
  CountingSink second;
  SinkGuard guard(&second);
  const std::uint64_t base2 = layout::invalidationCount();
  layout::setBatched(tree, 1, w, 99.0f);
  return layout::invalidationCount() == base2 + 1 && second.calls == 1;
}

// Named case (Stage 3 regression, review MAJOR): two-tree interleave is
// isolated per tree. Tree A opens a scope and queues 50× on one node;
// tree B opens/closes its own scope in between. B's close must flush only
// B's entries, and A's batch must still coalesce to exactly delta 1 with
// no cross-tree leak (per-tree scope chain: closing one tree's scope never
// flushes another tree's queued work).
static bool check_two_tree_interleave_isolated() {
  layout::PropertyTree treeA;
  layout::PropertyTree treeB;
  CountingSink sink;
  SinkGuard guard(&sink);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scopeA(treeA);
    for (int i = 0; i < 50; ++i)
      layout::setBatched(treeA, 1, w, static_cast<float>(i));
    {
      layout::BatchScope scopeB(treeB);
      layout::setBatched(treeB, 7, w, 1.0f);
    }  // B closes: flushes only B's entry.
    if (layout::invalidationCount() != base + 1) return false;
    if (sink.calls != 1) return false;
    if (sink.roots.size() != 1 || sink.roots[0] != 7) return false;
    // A's work is still queued, untouched by B's close.
    if (scopeA.pendingCount() != 1) return false;
  }  // A closes: flushes only A's coalesced entry.
  if (layout::invalidationCount() != base + 2) return false;
  if (sink.calls != 2) return false;
  if (sink.roots.size() != 2) return false;
  // A's 50× batch yielded exactly delta 1 (one delivery naming node 1)
  // and B's entry did not leak into it.
  bool sawA = false, sawB = false;
  for (layout::NodeId r : sink.roots) {
    if (r == 1) sawA = true;
    if (r == 7) sawB = true;
  }
  return sawA && sawB;
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
  RUN_CASE(check_50_sets_one_node_delta_1_single_descriptor);
  RUN_CASE(check_3_subtree_batch_delta_3);
  RUN_CASE(check_batched_no_per_mutation_allocation);
  RUN_CASE(check_pending_count_tracks_queued_work);
  RUN_CASE(check_nullptr_sink_restores_counting_default);
  RUN_CASE(check_two_tree_interleave_isolated);
  if (failures > 0) {
    std::printf("batch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("batch_test: all passing\n");
  return 0;
}
