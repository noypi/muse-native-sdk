// FR-DEPS-013 T6 (FR-DEPS-008 D1): Yoga adapter behind the LayoutEngine
// seam. Sole production TU allowed to include Yoga headers (TC-DEPS-049:
// `tools/deps/seam_check.py` fails any Yoga include outside the seam
// files). Builds with and without the vendored snapshot: when the Yoga
// headers are absent (interim, T5 link pending) YogaAvailable() is false
// and YogaComputeBoxes() returns false so the in-tree engine stays live.

#include "runtime/yoga_layout.h"

#include <cstdint>

#if defined(__has_include)
#if __has_include("yoga/Yoga.h")
#define MUSE_HAVE_YOGA 1
#include "yoga/Yoga.h"
#endif
#endif

namespace runtime {

bool YogaAvailable() {
#ifdef MUSE_HAVE_YOGA
  return true;
#else
  return false;
#endif
}

#ifdef MUSE_HAVE_YOGA
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
#endif  // MUSE_HAVE_YOGA

bool YogaComputeBoxes(const RuntimeTree& tree, NodeId root,
                      std::vector<LayoutBox>& boxes) {
#ifdef MUSE_HAVE_YOGA
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
#else
  (void)tree;
  (void)root;
  (void)boxes;
  return false;
#endif
}

}  // namespace runtime
