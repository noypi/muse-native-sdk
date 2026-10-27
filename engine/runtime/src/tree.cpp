#include "runtime/tree.h"

#include <cstddef>
#include <vector>

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
// array (orphan, including parent < -1), self-parent (parent == self
// index), or any component unreachable from the root (detached cycle or
// detached subtree).
NodeId instantiate(const CompiledNode* nodes, std::size_t count,
                  RuntimeTree& out) {
  out.nodes_.clear();
  if (count == 0 || nodes == nullptr) {
    return kInvalidNodeId;
  }
  NodeId root = kInvalidNodeId;
  for (std::size_t i = 0; i < count; ++i) {
    const int parent = nodes[i].parent;
    if (parent == static_cast<int>(i)) {
      out.nodes_.clear();
      return kInvalidNodeId;  // self-parent cycle
    }
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
  // Reject any component unreachable from the root (detached cycles or
  // detached subtrees would otherwise recurse unboundedly in layout).
  // Iterative DFS from the root over parent->child edges; O(n^2) is fine
  // for validation-time input sizes.
  {
    std::vector<char> visited(count, 0);
    visited[static_cast<std::size_t>(root)] = 1;
    std::size_t visited_count = 1;
    std::vector<std::size_t> stack;
    stack.push_back(static_cast<std::size_t>(root));
    while (!stack.empty()) {
      const std::size_t cur = stack.back();
      stack.pop_back();
      for (std::size_t i = 0; i < count; ++i) {
        if (nodes[i].parent == static_cast<int>(cur) &&
            !visited[i]) {
          visited[i] = 1;
          ++visited_count;
          stack.push_back(i);
        }
      }
    }
    if (visited_count != count) {
      out.nodes_.clear();
      return kInvalidNodeId;  // detached/unreachable component
    }
  }
  out.nodes_.resize(count);
  for (std::size_t i = 0; i < count; ++i) {
    Node& node = out.nodes_[i];
    node.id = static_cast<NodeId>(i);
    node.parent = nodes[i].parent;
    node.dirty = false;
    node.tag = nodes[i].tag;
    node.style = nodes[i].style;
  }
  return root;
}

}  // namespace runtime
