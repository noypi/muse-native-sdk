#include "runtime/layout.h"

#include <stdexcept>
#include <vector>

namespace runtime {
namespace {

// Exhaustive direction check with no `default`: adding an enumerator
// without handling it breaks the build under -Werror (-Wswitch).
bool isColumn(Direction direction) {
  switch (direction) {
    case Direction::Column:
      return true;
    case Direction::Row:
      return false;
  }
  throw std::invalid_argument("unknown Direction value");
}

float justifyOffset(Justify justify, float freeSpace) {
  switch (justify) {
    case Justify::Start:
      return 0.0f;
    case Justify::Center:
      return freeSpace / 2.0f;
    case Justify::End:
      return freeSpace;
  }
  throw std::invalid_argument("unknown Justify value");
}

float alignOffset(Align align, float freeSpace) {
  switch (align) {
    case Align::Start:
      return 0.0f;
    case Align::Center:
      return freeSpace / 2.0f;
    case Align::End:
      return freeSpace;
  }
  throw std::invalid_argument("unknown Align value");
}

}  // namespace

void LayoutEngine::validateStyle(const Style& style) {
  // Each helper throws on values outside the enumerator range (e.g. from
  // static_cast<Direction>(999)); the return values are discarded here.
  (void)isColumn(style.direction);
  (void)justifyOffset(style.justify, 0.0f);
  (void)alignOffset(style.align, 0.0f);
}

LayoutBox LayoutEngine::compute(const RuntimeTree& tree, NodeId root) {
  // Unknown root throws (mirrors RuntimeTree::get); empty tree has no
  // valid root so this throws too instead of returning a zero box.
  const Node& rootNode = tree.get(root);
  validateStyle(rootNode.style);

  boxes_.clear();
  boxes_.resize(tree.size());
  has_computed_ = false;

  layoutNode(tree, root, 0.0f, 0.0f, rootNode.style.width,
             rootNode.style.height);

  has_computed_ = true;
  return boxes_.at(static_cast<std::size_t>(root));
}

const LayoutBox& LayoutEngine::boxOf(NodeId id) const {
  if (!has_computed_) {
    throw std::out_of_range("layout has not been computed");
  }
  return boxes_.at(static_cast<std::size_t>(id));
}

// T6: dirty-subtree walk only (RULE-RUNTIME-002). Recomputes exactly the
// nodes under `dirtyRoot` via the shared layoutNode step, leaving cached
// boxes of clean siblings untouched. Ancestors keep their frames (child
// sizes are unchanged through this channel), so they are not re-entered
// and not counted. Unknown ids are a safe no-op returning 0.
std::size_t LayoutEngine::relayoutSubtree(RuntimeTree& tree,
                                          NodeId dirtyRoot) {
  if (dirtyRoot == kInvalidNodeId ||
      static_cast<std::size_t>(dirtyRoot) >= tree.size()) {
    return 0;
  }
  if (boxes_.size() < tree.size()) {
    boxes_.resize(tree.size());
  }

  // Collect the dirty subtree in parent-index order (stable, id order).
  std::vector<NodeId> subtree;
  subtree.push_back(dirtyRoot);
  for (std::size_t i = 0; i < subtree.size(); ++i) {
    const NodeId parent = subtree[i];
    for (std::size_t j = 0; j < tree.size(); ++j) {
      const NodeId candidate = static_cast<NodeId>(j);
      if (tree.get(candidate).parent == static_cast<int>(parent)) {
        subtree.push_back(candidate);
      }
    }
  }

  // Recompute the subtree from its cached frame so siblings stay cached.
  const LayoutBox frame = boxes_.at(static_cast<std::size_t>(dirtyRoot));
  layoutNode(tree, dirtyRoot, frame.x, frame.y, frame.width, frame.height);
  has_computed_ = true;

  // Clear dirty flags within the touched subtree (const_cast keeps this
  // inside layout.cpp; tree.h is owned by T2 — same pattern as frame.cpp).
  for (NodeId id : subtree) {
    if (tree.get(id).dirty) {
      const_cast<Node&>(tree.get(id)).dirty = false;
    }
  }
  return subtree.size();
}

void LayoutEngine::layoutNode(const RuntimeTree& tree, NodeId id, float x,
                              float y, float width, float height) {
  const Node& node = tree.get(id);
  validateStyle(node.style);

  // Collect children in id order (stable input order).
  std::vector<NodeId> children;
  for (std::size_t i = 0; i < tree.size(); ++i) {
    const Node& candidate = tree.get(static_cast<NodeId>(i));
    if (candidate.parent == static_cast<int>(id)) {
      children.push_back(static_cast<NodeId>(i));
    }
  }

  boxes_.at(static_cast<std::size_t>(id)) = LayoutBox{x, y, width, height};

  if (children.empty()) {
    return;
  }

  const Style& style = node.style;
  const bool column = isColumn(style.direction);
  const float padding = style.padding;
  const float gap = style.gap;

  const float contentX = x + padding;
  const float contentY = y + padding;
  float contentW = width - 2.0f * padding;
  float contentH = height - 2.0f * padding;
  if (contentW < 0.0f) contentW = 0.0f;
  if (contentH < 0.0f) contentH = 0.0f;

  const float contentMain = column ? contentH : contentW;
  const float contentCross = column ? contentW : contentH;

  // Fixed main-axis sizes and flex weights.
  float fixedTotal = 0.0f;
  float marginTotal = 0.0f;
  float flexTotal = 0.0f;
  for (NodeId child : children) {
    const Style& cs = tree.get(child).style;
    validateStyle(cs);
    marginTotal += 2.0f * cs.margin;
    if (cs.flex > 0.0f) {
      flexTotal += cs.flex;
    } else {
      fixedTotal += column ? cs.height : cs.width;
    }
  }
  const float gapTotal =
      children.size() > 1 ? gap * (children.size() - 1) : 0.0f;

  float remaining = contentMain - fixedTotal - marginTotal - gapTotal;
  if (remaining < 0.0f) remaining = 0.0f;

  // Main-axis size per child (fixed wins, remainder splits by flex).
  std::vector<float> mainSizes(children.size(), 0.0f);
  for (std::size_t i = 0; i < children.size(); ++i) {
    const Style& cs = tree.get(children[i]).style;
    if (cs.flex > 0.0f && flexTotal > 0.0f) {
      mainSizes[i] = remaining * (cs.flex / flexTotal);
    } else {
      mainSizes[i] = column ? cs.height : cs.width;
    }
  }

  float used = marginTotal + gapTotal;
  for (float s : mainSizes) used += s;
  float freeSpace = contentMain - used;
  if (freeSpace < 0.0f) freeSpace = 0.0f;
  float cursor = (column ? contentY : contentX) + justifyOffset(style.justify, freeSpace);

  for (std::size_t i = 0; i < children.size(); ++i) {
    const NodeId child = children[i];
    const Style& cs = tree.get(child).style;
    const float main = mainSizes[i];
    const float cross = column ? cs.width : cs.height;

    const float mainPos = cursor + cs.margin;
    float crossFree = contentCross - cross - 2.0f * cs.margin;
    if (crossFree < 0.0f) crossFree = 0.0f;
    const float crossPos =
        (column ? contentX : contentY) + cs.margin + alignOffset(style.align, crossFree);

    float childX, childY, childW, childH;
    if (column) {
      childX = crossPos;
      childY = mainPos;
      childW = cross;
      childH = main;
    } else {
      childX = mainPos;
      childY = crossPos;
      childW = main;
      childH = cross;
    }
    layoutNode(tree, child, childX, childY, childW, childH);
    cursor = mainPos + main + cs.margin;
    if (i + 1 < children.size()) {
      cursor += gap;
    }
  }
}

}  // namespace runtime
