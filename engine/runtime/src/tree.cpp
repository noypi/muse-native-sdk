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

// T1 stub: real instantiate lands in T2. Accepts empty input as an empty
// tree; any non-empty input is rejected until T2 implements the pass.
NodeId instantiate(const CompiledNode* nodes, std::size_t count,
                  RuntimeTree& out) {
  out.nodes_.clear();
  if (count == 0) {
    return kInvalidNodeId;
  }
  if (nodes == nullptr) {
    return kInvalidNodeId;
  }
  return kInvalidNodeId;
}

}  // namespace runtime
