#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "layout/window.h"

namespace layout {

// Root handle for an inspector-tracked solved tree.
using RootId = std::uint32_t;

inline constexpr RootId kInvalidRootId =
    static_cast<RootId>(0xFFFFFFFFu);

// Solved geometry for one node.
struct NodeGeom {
  std::uint32_t node = 0;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// One active Cassowary variable observed at dump time.
struct ConstraintVar {
  std::uint32_t node = 0;
  int attribute = 0;
  float value = 0.0f;
  float weight = 1.0f;
};

// One resolved token value with its cascade source.
struct ResolvedToken {
  std::uint32_t node = 0;
  std::uint32_t token = 0;
  float value = 0.0f;
  std::uint32_t cascadeSource = 0;
};

// Last invalidation reason recorded for a node.
struct InvalidationRecord {
  std::uint32_t node = 0;
  std::uint32_t reason = 0;
};

using NodeGeomList = std::vector<NodeGeom>;
using ConstraintVarList = std::vector<ConstraintVar>;
using ResolvedTokenList = std::vector<ResolvedToken>;
using InvalidationRecordList = std::vector<InvalidationRecord>;

// Single dump payload serving both the FR-LAYOUT-005 dev dump and the
// FR-LAYOUT-015 on-demand dump (plan A2).
struct InspectorDump {
  NodeGeomList nodes;
  ConstraintVarList constraints;
  ResolvedTokenList tokens;
  InvalidationRecordList reasons;
};

// Fixture entry point (plan names no constructor): builds a solved tree of
// the requested size with >= 1 constraint variable, >= 1 resolved token
// with cascade source, and one invalidation reason per node. Dev builds
// only; release throws LayoutError::kDisabledInRelease.
RootId createInspectorTree(int nodeCount);

// Inspector dump of the solved tree/constraints/tokens/invalidation
// (FR-LAYOUT-005, FR-LAYOUT-015). Dev builds only; release builds throw
// LayoutError::kDisabledInRelease. Unknown roots throw
// LayoutError::kUnknownWindow (distinct from kDisabledInRelease) and never
// return a partial payload.
InspectorDump dumpLayoutTree(RootId root);

// Source handle proving same-object provenance: the dumped node's identity.
using GeomHandle = std::uint32_t;

// One overlay rect bound to its source node geometry.
struct OverlayRect {
  GeomHandle source = 0;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// Overlay frame: one rect per committed node box.
struct OverlayFrame {
  std::vector<OverlayRect> bounds;
};

// Inspector release gate (T6, FR-LAYOUT-015 AC-LAYOUT-049). Reports whether
// the dev-only dump path is live: false in release (NDEBUG, hooks compiled
// out), true in dev. No error surface; never throws.
bool inspectorEnabled();

// Overhead counter proving the release no-op path was taken: 0 in release
// (NDEBUG) and stays 0 across repeated calls. Dev builds return the
// accumulated hook cost (callable; value documents hook cost). No error
// surface; never throws.
std::uint64_t inspectorOverheadNs();

// Inspector overlay (T7, FR-LAYOUT-015 AC-LAYOUT-050). Reads the committed
// solve output for the root and returns one rect per node box, each carrying
// the source GeomHandle (the node's identity). Never re-solves: repeated
// builds over the same root are stable. Dev builds only; release throws
// LayoutError::kDisabledInRelease. Unknown roots throw
// LayoutError::kUnknownWindow and never return a partial frame.
OverlayFrame buildOverlayFrame(RootId root);

}  // namespace layout
