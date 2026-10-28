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
#include <stdexcept>
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

// Named case (Stage 4 security, LOW-1): a throwing sink must not
// terminate the `noexcept` batch path. Encodes the `emitOne` guard in
// `engine/layout/src/batch.cpp` (read-only): delivery still counts once
// per touched subtree even when the observer throws, and the scope
// closes cleanly. Covers both the coalesced (in-scope) and immediate
// (out-of-scope) emit limbs.
class ThrowingSink : public layout::InvalidationSink {
 public:
  void onInvalidated(layout::NodeId) override {
    ++attempts;
    throw std::runtime_error("hostile observer");
  }
  int attempts = 0;
};

static bool check_throwing_sink_does_not_terminate() {
  layout::PropertyTree tree;
  ThrowingSink sink;
  SinkGuard guard(&sink);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scope(tree);
    for (int i = 0; i < 10; ++i)
      layout::setBatched(tree, 1, w, static_cast<float>(i));
  }  // Scope close flushes through the throwing sink: must not terminate.
  if (layout::invalidationCount() != base + 1) return false;
  if (sink.attempts != 1) return false;
  // Immediate-emit limb (no open scope) with the same throwing sink.
  layout::setBatched(tree, 2, w, 1.0f);
  if (layout::invalidationCount() != base + 2) return false;
  return sink.attempts == 2;
}

// Named case (Stage 4 security, LOW-2a): sink-reset protocol. The
// documented contract in `engine/layout/include/layout/batch.h`
// (read-only) requires the sink to outlive its installation or be reset
// to `nullptr` first. Reset-then-destroy must be safe: later emissions
// advance the counter with no delivery and no UAF call, and a fresh
// sink installs cleanly afterwards (global state is not stuck).
static bool check_sink_reset_then_destroy_no_uaf() {
  layout::PropertyTree tree;
  auto* doomed = new CountingSink();
  doomed->roots.reserve(8);
  layout::setInvalidationSink(doomed);
  layout::setInvalidationSink(nullptr);
  delete doomed;
  doomed = nullptr;
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  layout::setBatched(tree, 1, w, 1.0f);  // Would UAF-call if reset failed.
  if (layout::invalidationCount() != base + 1) return false;
  CountingSink second;
  SinkGuard guard(&second);
  layout::setBatched(tree, 2, w, 2.0f);
  return second.calls == 1 && second.roots.size() == 1 &&
         second.roots[0] == 2;
}

// Named case (Stage 4 security, LOW-2b): tree-slot reuse must not
// cross-route. The contract requires a tree to outlive its open scopes;
// once a scope closes, its table slot is released, so a later tree
// (even one allocated at the same address) starts with a clean slot and
// its invalidations route only to itself.
static bool check_tree_slot_reuse_no_cross_route() {
  CountingSink sink;
  SinkGuard guard(&sink);
  sink.roots.reserve(8);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::PropertyTree treeA;
    {
      layout::BatchScope scopeA(treeA);
      layout::setBatched(treeA, 1, w, 1.0f);
    }  // Scope A closes: slot released before treeA dies.
  }
  {
    layout::PropertyTree treeB;
    layout::BatchScope scopeB(treeB);
    if (scopeB.pendingCount() != 0) return false;  // No stale dirty.
    layout::setBatched(treeB, 2, w, 2.0f);
    if (scopeB.pendingCount() != 1) return false;
  }
  if (layout::invalidationCount() != base + 2) return false;
  if (sink.calls != 2 || sink.roots.size() != 2) return false;
  bool sawA = false, sawB = false;
  for (layout::NodeId r : sink.roots) {
    if (r == 1) sawA = true;
    if (r == 2) sawB = true;
  }
  return sawA && sawB;
}

// Named case (Stage 4 security, table-limit degrade): the 512-dirty
// bound degrades to immediate emit instead of dropping or spamming.
// Bounded facsimile at the real limit: 600 distinct node ids in one
// scope (512 fill the table, 88 degrade to immediate emit, 512 flush at
// close) ⇒ exactly 600 counted deliveries — bounded, none lost, none
// duplicated. Runs fast: 600 in-process sets, no large allocation
// (PropertyTree holds a fixed 512 node slots; sets past capacity are
// dropped deterministically per `property.h`).
static bool check_dirty_table_limit_degrades_to_emit() {
  layout::PropertyTree tree;
  CountingSink sink;
  SinkGuard guard(&sink);
  sink.roots.reserve(640);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope scope(tree);
    for (int i = 1; i <= 600; ++i)
      layout::setBatched(tree, static_cast<layout::NodeId>(i), w,
                         static_cast<float>(i));
  }
  if (layout::invalidationCount() != base + 600) return false;
  return sink.calls == 600;
}

// Named case (Stage 4 security, table-limit degrade): the 8-tree bound
// degrades to immediate emit. Bounded facsimile at the real limit: 9
// trees scoped at once; the 9th finds no free slot, so its mutation
// emits immediately (pending 0) while the first 8 stay queued (pending
// 1 each). Close order flushes each tree's own entry only ⇒ total
// delta 9, one delivery per tree, no cross-tree leak.
static bool check_tree_table_limit_degrades_to_emit() {
  layout::PropertyTree t0, t1, t2, t3, t4, t5, t6, t7, t8;
  CountingSink sink;
  SinkGuard guard(&sink);
  sink.roots.reserve(16);
  const layout::PropHandle w = layout::prop("width");
  const std::uint64_t base = layout::invalidationCount();
  {
    layout::BatchScope s0(t0);
    layout::BatchScope s1(t1);
    layout::BatchScope s2(t2);
    layout::BatchScope s3(t3);
    layout::BatchScope s4(t4);
    layout::BatchScope s5(t5);
    layout::BatchScope s6(t6);
    layout::BatchScope s7(t7);
    layout::BatchScope s8(t8);  // No free slot: untracked.
    layout::setBatched(t0, 1, w, 1.0f);
    layout::setBatched(t1, 1, w, 1.0f);
    layout::setBatched(t2, 1, w, 1.0f);
    layout::setBatched(t3, 1, w, 1.0f);
    layout::setBatched(t4, 1, w, 1.0f);
    layout::setBatched(t5, 1, w, 1.0f);
    layout::setBatched(t6, 1, w, 1.0f);
    layout::setBatched(t7, 1, w, 1.0f);
    layout::setBatched(t8, 1, w, 1.0f);  // Immediate emit (untracked).
    if (layout::invalidationCount() != base + 1) return false;
    if (sink.calls != 1) return false;
    if (s0.pendingCount() != 1 || s1.pendingCount() != 1 ||
        s2.pendingCount() != 1 || s3.pendingCount() != 1 ||
        s4.pendingCount() != 1 || s5.pendingCount() != 1 ||
        s6.pendingCount() != 1 || s7.pendingCount() != 1) {
      return false;
    }
    if (s8.pendingCount() != 0) return false;
  }
  if (layout::invalidationCount() != base + 9) return false;
  return sink.calls == 9;
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
  RUN_CASE(check_throwing_sink_does_not_terminate);
  RUN_CASE(check_sink_reset_then_destroy_no_uaf);
  RUN_CASE(check_tree_slot_reuse_no_cross_route);
  RUN_CASE(check_dirty_table_limit_degrades_to_emit);
  RUN_CASE(check_tree_table_limit_degrades_to_emit);
  if (failures > 0) {
    std::printf("batch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("batch_test: all passing\n");
  return 0;
}
