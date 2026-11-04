// FR-DEPS-013 (mandatory Yoga, fail-closed): Yoga adapter behind the
// LayoutEngine seam. Sole production TU allowed to include Yoga headers
// (TC-DEPS-049: `tools/deps/seam_check.py` fails any Yoga include outside
// the seam files). Yoga is the ONLY prod layout backend: the build always
// defines MUSE_HAVE_YOGA (engine/runtime/CMakeLists.txt) and links the T5
// per-ABI yogacore static lib; the vendored snapshot must be staged
// ($MUSE_NATIVE_THIRD_PARTY_DIR/yoga-3.2.1) or configure fails. There is no
// silent in-tree fallback: without the build definition this TU fails
// compilation at the #error below, and YogaComputeBoxes() never reports
// "not linked". The in-tree flexbox-subset engine in `layout.cpp` remains
// only as the T8 parity reference (non-prod, never invoked on this path).

#include "runtime/yoga_layout.h"

#include <cstdint>

#ifndef MUSE_HAVE_YOGA
#error "MUSE Yoga is mandatory: MUSE_HAVE_YOGA was not defined by the build (engine/runtime/CMakeLists.txt must define it; MUSE_HAVE_YOGA=OFF is rejected at configure)."
#endif

#include "yoga/Yoga.h"

namespace runtime {

bool YogaAvailable() { return true; }

namespace {

// Yoga C API mapping (pinned to the vendored v3.2.1 headers): fixed
// main-axis sizes win, remainder splits by flex weight, padding/gap/margin
// mirror the in-tree engine in layout.cpp.

YGNodeRef AttachNode(const RuntimeTree& tree, NodeId id) {
  const Node& node = tree.get(id);
  YGNodeRef yn = YGNodeNew();
  const Style& style = node.style;
  YGNodeStyleSetFlexDirection(
      yn, style.direction == Direction::Column ? YGFlexDirectionColumn
                                               : YGFlexDirectionRow);
  switch (style.justify) {
    case Justify::Start:
      YGNodeStyleSetJustifyContent(yn, YGJustifyFlexStart);
      break;
    case Justify::Center:
      YGNodeStyleSetJustifyContent(yn, YGJustifyCenter);
      break;
    case Justify::End:
      YGNodeStyleSetJustifyContent(yn, YGJustifyFlexEnd);
      break;
  }
  switch (style.align) {
    case Align::Start:
      YGNodeStyleSetAlignItems(yn, YGAlignFlexStart);
      break;
    case Align::Center:
      YGNodeStyleSetAlignItems(yn, YGAlignCenter);
      break;
    case Align::End:
      YGNodeStyleSetAlignItems(yn, YGAlignFlexEnd);
      break;
  }
  YGNodeStyleSetWidth(yn, style.width);
  YGNodeStyleSetHeight(yn, style.height);
  if (style.flex > 0.0f) {
    YGNodeStyleSetFlexGrow(yn, style.flex);
  }
  YGNodeStyleSetMargin(yn, YGEdgeAll, style.margin);
  YGNodeStyleSetPadding(yn, YGEdgeAll, style.padding);
  YGNodeStyleSetGap(yn, YGGutterRow, style.gap);
  YGNodeStyleSetGap(yn, YGGutterColumn, style.gap);

  std::uint32_t index = 0;
  for (std::size_t i = 0; i < tree.size(); ++i) {
    const NodeId candidate = static_cast<NodeId>(i);
    if (tree.get(candidate).parent == static_cast<int>(id)) {
      YGNodeInsertChild(yn, AttachNode(tree, candidate), index++);
    }
  }
  return yn;
}

void ReadBack(YGNodeRef yn, std::vector<LayoutBox>& boxes) {
  const std::uint32_t count = YGNodeGetChildCount(yn);
  for (std::uint32_t i = 0; i < count; ++i) {
    ReadBack(YGNodeGetChild(yn, i), boxes);
  }
  const NodeId id = static_cast<NodeId>(
      reinterpret_cast<std::uintptr_t>(YGNodeGetContext(yn)));
  if (id >= 0 && static_cast<std::size_t>(id) < boxes.size()) {
    boxes[static_cast<std::size_t>(id)] = LayoutBox{
        YGNodeLayoutGetLeft(yn), YGNodeLayoutGetTop(yn),
        YGNodeLayoutGetWidth(yn), YGNodeLayoutGetHeight(yn)};
  }
}

}  // namespace

bool YogaComputeBoxes(const RuntimeTree& tree, NodeId root,
                      std::vector<LayoutBox>& boxes) {
  // Unknown root mirrors LayoutEngine::compute (RuntimeTree::get throws).
  const Node& rootNode = tree.get(root);
  YGNodeRef yn = AttachNode(tree, root);
  YGNodeCalculateLayout(yn, rootNode.style.width, rootNode.style.height,
                        YGDirectionLTR);
  std::vector<LayoutBox> out(tree.size());
  // Pair YG children back to RuntimeTree ids via lockstep id-order walk
  // (AttachNode inserts children in id order, so the child indices agree).
  struct Walker {
    static void Tag(YGNodeRef yn, const RuntimeTree& tree, NodeId id) {
      YGNodeSetContext(yn, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
      std::uint32_t index = 0;
      for (std::size_t i = 0; i < tree.size(); ++i) {
        const NodeId candidate = static_cast<NodeId>(i);
        if (tree.get(candidate).parent == static_cast<int>(id)) {
          Tag(YGNodeGetChild(yn, index++), tree, candidate);
        }
      }
    }
  };
  Walker::Tag(yn, tree, root);
  ReadBack(yn, out);
  YGNodeFreeRecursive(yn);
  boxes = out;
  return true;
}

}  // namespace runtime
