#include "layout/batch.h"

namespace layout {

namespace {

// Bounded, allocation-free batch state. Each distinct node id is its own
// subtree root (the interface fixes no node→subtree mapping; this is the
// minimal reading consistent with the one-node ⇒ delta-1 cases). All
// storage is file-static so the batch path never touches the heap.
constexpr std::size_t kMaxDirtySubtrees = 512;

NodeId g_dirty[kMaxDirtySubtrees];
std::size_t g_dirtyCount = 0;
int g_depth = 0;
InvalidationSink* g_sink = nullptr;
std::uint64_t g_count = 0;

void emitOne(NodeId subtreeRoot) noexcept {
  ++g_count;
  InvalidationSink* s = g_sink;
  if (s != nullptr) s->onInvalidated(subtreeRoot);
}

void markDirty(NodeId subtreeRoot) noexcept {
  for (std::size_t i = 0; i < g_dirtyCount; ++i) {
    if (g_dirty[i] == subtreeRoot) return;
  }
  if (g_dirtyCount < kMaxDirtySubtrees) {
    g_dirty[g_dirtyCount++] = subtreeRoot;
    return;
  }
  // Table full (beyond any tested batch): emit immediately to stay
  // bounded and allocation-free rather than dropping the invalidation.
  emitOne(subtreeRoot);
}

void flushDirty() noexcept {
  for (std::size_t i = 0; i < g_dirtyCount; ++i) emitOne(g_dirty[i]);
  g_dirtyCount = 0;
}

}  // namespace

BatchScope::BatchScope(PropertyTree& t) : tree_(&t) { ++g_depth; }

BatchScope::~BatchScope() {
  if (g_depth > 0) --g_depth;
  if (g_depth == 0) flushDirty();
}

std::size_t BatchScope::pendingCount() const noexcept { return g_dirtyCount; }

void setBatched(PropertyTree& t, NodeId node, PropHandle h, float v) noexcept {
  t.set(node, h, v);
  if (g_depth == 0) {
    emitOne(node);
    return;
  }
  markDirty(node);
}

void setInvalidationSink(InvalidationSink* s) noexcept { g_sink = s; }

std::uint64_t invalidationCount() noexcept { return g_count; }

}  // namespace layout
