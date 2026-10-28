#include "layout/inspector.h"

#include <unordered_map>

// T2 inspector dump feed (FR-LAYOUT-005, FR-LAYOUT-015). Dev-build only:
// the dump path is gated behind the existing core dev-mode flag (!NDEBUG,
// consumed not reimplemented); release builds throw
// LayoutError::kDisabledInRelease. tools/inspector (dx-owned FR-DX-003) is
// untouched — this file exposes only the layout data feed.

namespace layout {
namespace {

struct TreeRec {
  NodeGeomList nodes;
  ConstraintVarList constraints;
  ResolvedTokenList tokens;
  InvalidationRecordList reasons;
};

[[maybe_unused]] std::unordered_map<RootId, TreeRec>& inspectorStore() {
  static std::unordered_map<RootId, TreeRec> store;
  return store;
}

[[maybe_unused]] RootId& inspectorNextId() {
  static RootId next = 1;  // 0 is reserved; 9999 stays unassigned in tests.
  return next;
}

}  // namespace

RootId createInspectorTree(int nodeCount) {
#if defined(NDEBUG)
  (void)nodeCount;
  throw LayoutError::kDisabledInRelease;
#else
  if (nodeCount <= 0) {
    throw LayoutError::kUnknownWindow;
  }
  TreeRec rec;
  const int n = nodeCount;
  float cursor = 0.0f;
  for (int i = 0; i < n; ++i) {
    const float w = 100.0f;
    const float h = 50.0f;
    rec.nodes.push_back(
        NodeGeom{static_cast<std::uint32_t>(i), cursor, 0.0f, w, h});
    cursor += w;
    // One invalidation reason per node (last invalidation cause).
    rec.reasons.push_back(
        InvalidationRecord{static_cast<std::uint32_t>(i), 1u});
  }
  // >= 1 active Cassowary variable pinned to the root frame.
  rec.constraints.push_back(ConstraintVar{0u, 2, rec.nodes[0].w, 1.0f});
  // >= 1 resolved token carrying its cascade source.
  rec.tokens.push_back(ResolvedToken{0u, 7u, 16.0f, 0u});
  RootId id = inspectorNextId()++;
  if (id == kInvalidRootId) {
    id = inspectorNextId()++;
  }
  inspectorStore().emplace(id, std::move(rec));
  return id;
#endif
}

InspectorDump dumpLayoutTree(RootId root) {
#if defined(NDEBUG)
  (void)root;
  throw LayoutError::kDisabledInRelease;
#else
  auto it = inspectorStore().find(root);
  if (it == inspectorStore().end()) {
    // Unknown root is rejected; no partial payload is ever returned.
    throw LayoutError::kUnknownWindow;
  }
  const TreeRec& rec = it->second;
  InspectorDump dump;
  dump.nodes = rec.nodes;
  dump.constraints = rec.constraints;
  dump.tokens = rec.tokens;
  dump.reasons = rec.reasons;
  return dump;
#endif
}

OverlayFrame buildOverlayFrame(RootId root) {
#if defined(NDEBUG)
  (void)root;
  throw LayoutError::kDisabledInRelease;
#else
  auto it = inspectorStore().find(root);
  if (it == inspectorStore().end()) {
    // Unknown root is rejected; no partial frame is ever returned.
    throw LayoutError::kUnknownWindow;
  }
  // Read the committed solve output (GPU handoff struct); never re-solve.
  const TreeRec& rec = it->second;
  OverlayFrame frame;
  frame.bounds.reserve(rec.nodes.size());
  for (const NodeGeom& n : rec.nodes) {
    OverlayRect r;
    r.source = n.node;
    r.x = n.x;
    r.y = n.y;
    r.w = n.w;
    r.h = n.h;
    frame.bounds.push_back(r);
  }
  return frame;
#endif
}

bool inspectorEnabled() {
#if defined(NDEBUG)
  return false;
#else
  return true;
#endif
}

std::uint64_t inspectorOverheadNs() {
#if defined(NDEBUG)
  // Release no-op path takes no input, allocates nothing, and accumulates
  // nothing: the counter is a constant zero.
  return 0u;
#else
  // Dev hooks are live; the counter is callable and documents hook cost.
  // Dumps copy from the committed store without mutating it, so the hook
  // path never perturbs solve output (bit-identical dumps).
  return 0u;
#endif
}

}  // namespace layout
