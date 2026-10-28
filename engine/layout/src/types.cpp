#include "layout/types.h"

namespace layout {

std::uint32_t Tree::nodeCount() const noexcept {
  return static_cast<std::uint32_t>(nodes_.size());
}

bool Tree::contains(NodeId id) const noexcept {
  for (NodeId stored : nodes_) {
    if (stored == id) {
      return true;
    }
  }
  return false;
}

NodeId allocateNode(Tree& tree, NodeId parent) noexcept {
  try {
    if (parent != kInvalidNodeId && !tree.contains(parent)) {
      return kInvalidNodeId;
    }
    if (tree.next_ == kInvalidNodeId) {
      return kInvalidNodeId;
    }
    const NodeId id = tree.next_++;
    tree.nodes_.push_back(id);
    return id;
  } catch (...) {
    return kInvalidNodeId;
  }
}

}  // namespace layout
