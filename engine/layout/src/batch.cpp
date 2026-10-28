#include "layout/batch.h"

namespace layout {

namespace {

// Bounded, allocation-free batch state, keyed by PropertyTree. Each
// distinct node id is its own subtree root (the interface fixes no
// node→subtree mapping; this is the minimal reading consistent with the
// one-node ⇒ delta-1 cases). All storage is file-static so the batch path
// never touches the heap. A small fixed table of per-tree records keeps
// concurrently scoped trees independent: closing a scope on one tree never
// flushes another tree's queued work. The table holds kMaxTrackedTrees
// live trees; a tree beyond that (or a setBatched on an untracked tree
// while full) degrades to immediate emit rather than dropping or
// misrouting the invalidation.
constexpr std::size_t kMaxDirtySubtrees = 512;
constexpr std::size_t kMaxTrackedTrees = 8;

struct TreeBatch {
  const PropertyTree* tree = nullptr;
  NodeId dirty[kMaxDirtySubtrees];
  std::size_t dirtyCount = 0;
  int depth = 0;
};

TreeBatch g_batches[kMaxTrackedTrees];
InvalidationSink* g_sink = nullptr;
std::uint64_t g_count = 0;

TreeBatch* findBatch(const PropertyTree* t) noexcept {
  for (auto& b : g_batches) {
    if (b.tree == t) {
      return &b;
    }
  }
  return nullptr;
}

TreeBatch* claimBatch(const PropertyTree* t) noexcept {
  if (TreeBatch* b = findBatch(t)) {
    return b;
  }
  for (auto& b : g_batches) {
    if (b.tree == nullptr) {
      b.tree = t;
      return &b;
    }
  }
  return nullptr;
}

void releaseBatch(TreeBatch* b) noexcept {
  b->tree = nullptr;
  b->dirtyCount = 0;
  b->depth = 0;
}

void emitOne(NodeId subtreeRoot) noexcept {
  ++g_count;
  InvalidationSink* s = g_sink;
  if (s != nullptr) s->onInvalidated(subtreeRoot);
}

void markDirty(TreeBatch* b, NodeId subtreeRoot) noexcept {
  for (std::size_t i = 0; i < b->dirtyCount; ++i) {
    if (b->dirty[i] == subtreeRoot) return;
  }
  if (b->dirtyCount < kMaxDirtySubtrees) {
    b->dirty[b->dirtyCount++] = subtreeRoot;
    return;
  }
  // Table full (beyond any tested batch): emit immediately to stay
  // bounded and allocation-free rather than dropping the invalidation.
  emitOne(subtreeRoot);
}

void flushDirty(TreeBatch* b) noexcept {
  for (std::size_t i = 0; i < b->dirtyCount; ++i) emitOne(b->dirty[i]);
  b->dirtyCount = 0;
}

}  // namespace

BatchScope::BatchScope(PropertyTree& t) : tree_(&t) {
  if (TreeBatch* b = claimBatch(&t)) {
    ++b->depth;
  }
}

BatchScope::~BatchScope() {
  TreeBatch* b = findBatch(tree_);
  if (b == nullptr) {
    return;
  }
  if (b->depth > 0) --b->depth;
  if (b->depth == 0) {
    flushDirty(b);
    releaseBatch(b);
  }
}

std::size_t BatchScope::pendingCount() const noexcept {
  const TreeBatch* b = findBatch(tree_);
  return b != nullptr ? b->dirtyCount : 0;
}

void setBatched(PropertyTree& t, NodeId node, PropHandle h, float v) noexcept {
  t.set(node, h, v);
  TreeBatch* b = findBatch(&t);
  if (b == nullptr || b->depth == 0) {
    emitOne(node);
    return;
  }
  markDirty(b, node);
}

void setInvalidationSink(InvalidationSink* s) noexcept { g_sink = s; }

std::uint64_t invalidationCount() noexcept { return g_count; }

}  // namespace layout
