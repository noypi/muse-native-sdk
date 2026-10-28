// Batch 3 T7 contract tests — FR-LAYOUT-015 AC-LAYOUT-050 (TC-050).
// Inspector overlay reflects renderer geometry (dev builds).
// Hermetic: C++17 + layout/inspector.h only; no network, no env/config reads,
// no filesystem, no live services, no renderer/GPU. The "renderer geometry"
// oracle is the committed solve output observed through the public
// `dumpLayoutTree` boundary (T2): overlay rects must equal the dumped node
// boxes for the same root. The overlay never re-solves (plan Detail).
// Binds ONLY to the T7 code `Interface:` line (verbatim in contract T7.md):
//   `OverlayFrame buildOverlayFrame(RootId root) -> { OverlayRect[] bounds }`
//   — each rect carries the source `GeomHandle` proving same-object provenance.
//
// ASSUMPTION (type shapes): the plan names OverlayFrame/OverlayRect/GeomHandle
//   but not their fields or error channel. Tests assume:
//     `using GeomHandle = std::uint32_t;`
//     `struct OverlayRect { GeomHandle source; float x; float y; float w; float h; };`
//     `struct OverlayFrame { std::vector<OverlayRect> bounds; };`
//     `OverlayFrame layout::buildOverlayFrame(RootId root);`
//   declared in `layout/inspector.h`. If the coder settles different field
//   names, the contract reconciles the member access; the rect-equality and
//   same-object-provenance expectations do not change.
//
// ASSUMPTION (provenance): same-object provenance is observable as
//   `rect.source` identifying the dumped node it was built from (compared
//   against `NodeGeom::node` for the same index in the 5-node fixture where
//   node ids are 0..4). Reconciles if the coder carries the handle
//   differently (e.g. pointer-sized id); the equality-with-renderer-boxes
//   expectation stands.
//
// ASSUMPTION (unknown root): the plan names no error surface for the overlay.
//   Tests assume an unknown root throws `layout::LayoutError` (same throw
//   convention as dumpLayoutTree's kUnknownWindow) and never returns a partial
//   frame. Reconciles if the coder settles an empty-frame rule; the
//   5-node equality expectations do not change.
//
// T2 context (inspector.h/inspector.cpp NDEBUG gate) is read-only: these tests
// extend it, never weaken it. tools/inspector (dx-owned FR-DX-003) untouched.
#include <cstdint>
#include <cstdio>

#include "layout/inspector.h"

namespace {

// 5-node fixture per the acceptance criterion (AC-LAYOUT-050).
layout::RootId make_five_node_tree() {
  return layout::createInspectorTree(5);
}

bool rect_equals_node(const layout::OverlayRect& r,
                      const layout::NodeGeom& n) {
  return r.x == n.x && r.y == n.y && r.w == n.w && r.h == n.h;
}

}  // namespace

// AC-LAYOUT-050: overlay carries one rect per node for the 5-node fixture.
static bool check_overlay_has_five_rects() {
  layout::RootId root = make_five_node_tree();
  layout::OverlayFrame frame = layout::buildOverlayFrame(root);
  return frame.bounds.size() == 5;
}

// AC-LAYOUT-050: every overlay rect equals the renderer (committed solve)
// box for the same node — geometry equality incl. margins/padding/baselines
// as baked into the committed NodeGeom boxes.
static bool check_overlay_rects_equal_renderer_boxes() {
  layout::RootId root = make_five_node_tree();
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  layout::OverlayFrame frame = layout::buildOverlayFrame(root);
  if (dump.nodes.size() != 5) return false;
  if (frame.bounds.size() != dump.nodes.size()) return false;
  for (std::size_t i = 0; i < frame.bounds.size(); ++i) {
    if (!rect_equals_node(frame.bounds[i], dump.nodes[i])) return false;
  }
  return true;
}

// AC-LAYOUT-050: each rect carries the source GeomHandle proving same-object
// provenance (source matches the dumped node's identity).
static bool check_overlay_rects_carry_geom_handle() {
  layout::RootId root = make_five_node_tree();
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  layout::OverlayFrame frame = layout::buildOverlayFrame(root);
  if (frame.bounds.size() != dump.nodes.size()) return false;
  for (std::size_t i = 0; i < frame.bounds.size(); ++i) {
    if (frame.bounds[i].source != dump.nodes[i].node) return false;
  }
  return true;
}

// AC-LAYOUT-050: overlay reflects the committed output — a second build over
// the same root is stable (never re-solves to different boxes).
static bool check_overlay_is_stable_across_builds() {
  layout::RootId root = make_five_node_tree();
  layout::OverlayFrame first = layout::buildOverlayFrame(root);
  layout::OverlayFrame second = layout::buildOverlayFrame(root);
  if (first.bounds.size() != second.bounds.size()) return false;
  for (std::size_t i = 0; i < first.bounds.size(); ++i) {
    if (first.bounds[i].x != second.bounds[i].x) return false;
    if (first.bounds[i].y != second.bounds[i].y) return false;
    if (first.bounds[i].w != second.bounds[i].w) return false;
    if (first.bounds[i].h != second.bounds[i].h) return false;
    if (first.bounds[i].source != second.bounds[i].source) return false;
  }
  return true;
}

// Boundary: unknown root is rejected and never yields a partial frame.
static bool check_unknown_root_is_rejected() {
  try {
    layout::OverlayFrame frame =
        layout::buildOverlayFrame(static_cast<layout::RootId>(9999));
    (void)frame;
  } catch (const layout::LayoutError&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

// Boundary: single-node tree yields a single rect equal to the single box.
static bool check_single_node_overlay() {
  layout::RootId root = layout::createInspectorTree(1);
  layout::InspectorDump dump = layout::dumpLayoutTree(root);
  layout::OverlayFrame frame = layout::buildOverlayFrame(root);
  if (dump.nodes.size() != 1) return false;
  if (frame.bounds.size() != 1) return false;
  if (!rect_equals_node(frame.bounds[0], dump.nodes[0])) return false;
  return frame.bounds[0].source == dump.nodes[0].node;
}

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
  RUN_CASE(check_overlay_has_five_rects);
  RUN_CASE(check_overlay_rects_equal_renderer_boxes);
  RUN_CASE(check_overlay_rects_carry_geom_handle);
  RUN_CASE(check_overlay_is_stable_across_builds);
  RUN_CASE(check_unknown_root_is_rejected);
  RUN_CASE(check_single_node_overlay);
  if (failures > 0) {
    std::printf("overlay_geometry_test: %d failing\n", failures);
    return 1;
  }
  std::printf("overlay_geometry_test: all passing\n");
  return 0;
}
