#pragma once

#include <cstdint>
#include <vector>

namespace layout {

using NodeId = std::uint32_t;

inline constexpr NodeId kInvalidNodeId = static_cast<NodeId>(0xFFFFFFFFu);

struct Tree {
  std::uint32_t nodeCount() const noexcept;
  bool contains(NodeId id) const noexcept;

 private:
  friend NodeId allocateNode(Tree& tree, NodeId parent) noexcept;
  std::vector<NodeId> nodes_;
  NodeId next_ = 0;
};

NodeId allocateNode(Tree& tree, NodeId parent) noexcept;

}  // namespace layout
