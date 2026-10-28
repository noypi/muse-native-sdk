#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace input {

using NodeId = int;
using GroupId = int;

enum class Status {
  kOk,
  kNotFound,
};

struct A11yNode {
  NodeId id = -1;
  bool focused = false;
};

class FocusTree {
 public:
  FocusTree() = default;

  Status setOrder(std::vector<NodeId> order);
  NodeId next(NodeId from);
  NodeId prev(NodeId from);
  Status assignToGroup(NodeId node, GroupId group);
  Status enterGroup(GroupId group);
  Status exitGroup();
  std::vector<A11yNode> exposeToA11y() const;

 private:
  std::vector<NodeId> EffectiveOrder() const;

  std::vector<NodeId> order_;
  std::unordered_map<NodeId, GroupId> group_of_;
  bool trapped_ = false;
  GroupId active_group_ = -1;
  NodeId focused_ = -1;
};

}  // namespace input
