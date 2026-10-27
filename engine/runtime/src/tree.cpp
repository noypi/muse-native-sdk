#include "runtime/tree.h"

namespace runtime {

const Node& RuntimeTree::get(NodeId id) const {
  if (id == kInvalidNodeId || static_cast<std::size_t>(id) >= nodes_.size()) {
    throw std::out_of_range("unknown node id");
  }
  return nodes_[id];
}

std::size_t RuntimeTree::size() const { return nodes_.size(); }

void RuntimeTree::markDirty(NodeId id) {
  if (id == kInvalidNodeId || static_cast<std::size_t>(id) >= nodes_.size()) {
    return;
  }
  nodes_[id].dirty = true;
}

std::size_t RuntimeTree::dirtyCount() const {
  std::size_t count = 0;
  for (const Node& node : nodes_) {
    if (node.dirty) {
      ++count;
    }
  }
  return count;
}

bool RuntimeTree::isInstantiated(NodeId id) const {
  return id != kInvalidNodeId &&
         static_cast<std::size_t>(id) < nodes_.size();
}

// Instantiate a compiled tree into a live node tree with stable ids.
// Ids are input indices assigned once here and never reassigned on update.
// Validation (error code kInvalidNodeId, never a crash): empty input,
// null input, zero or duplicate roots, or any parent index outside the
// array (orphan, including parent < -1).
NodeId instantiate(const CompiledNode* nodes, std::size_t count,
                  RuntimeTree& out) {
  out.nodes_.clear();
  if (count == 0 || nodes == nullptr) {
    return kInvalidNodeId;
  }
  NodeId root = kInvalidNodeId;
  for (std::size_t i = 0; i < count; ++i) {
    const int parent = nodes[i].parent;
    if (parent == -1) {
      if (root != kInvalidNodeId) {
        out.nodes_.clear();
        return kInvalidNodeId;  // duplicate root
      }
      root = static_cast<NodeId>(i);
    } else if (parent < -1 || static_cast<std::size_t>(parent) >= count) {
      out.nodes_.clear();
      return kInvalidNodeId;  // orphan parent
    }
  }
  if (root == kInvalidNodeId) {
    return kInvalidNodeId;  // no root
  }
  out.nodes_.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    Node& node = out.nodes_[i];
    node.id = static_cast<NodeId>(i);
    node.parent = nodes[i].parent;
    node.dirty = false;
    node.tag = nodes[i].tag;
  }
  return root;
}

}  // namespace runtime
