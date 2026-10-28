#include "input/focus_tree.h"

#include <algorithm>

namespace input {

Status FocusTree::setOrder(std::vector<NodeId> order) {
  order_ = std::move(order);
  if (trapped_) {
    const std::vector<NodeId> eff = EffectiveOrder();
    if (eff.empty()) {
      trapped_ = false;
      active_group_ = -1;
    }
  }
  if (!order_.empty()) {
    // Keep focus if it is still in the order; otherwise head the ring.
    bool kept = false;
    for (NodeId id : order_) {
      if (id == focused_) {
        kept = true;
        break;
      }
    }
    if (!kept) focused_ = order_[0];
    // While trapped, focus must live inside the trapped subset.
    if (trapped_) {
      bool inside = false;
      for (NodeId id : EffectiveOrder()) {
        if (id == focused_) {
          inside = true;
          break;
        }
      }
      if (!inside) {
        const std::vector<NodeId> eff = EffectiveOrder();
        focused_ = eff.empty() ? order_[0] : eff[0];
      }
    }
  } else {
    focused_ = -1;
  }
  return Status::kOk;
}

NodeId FocusTree::next(NodeId from) {
  const std::vector<NodeId> eff = EffectiveOrder();
  if (eff.empty()) return from;
  std::size_t idx = 0;
  bool found = false;
  for (std::size_t i = 0; i < eff.size(); ++i) {
    if (eff[i] == from) {
      idx = i;
      found = true;
      break;
    }
  }
  if (!found) return from;  // Unknown id: focus unchanged.
  const NodeId out = eff[(idx + 1) % eff.size()];
  focused_ = out;
  return out;
}

NodeId FocusTree::prev(NodeId from) {
  const std::vector<NodeId> eff = EffectiveOrder();
  if (eff.empty()) return from;
  std::size_t idx = 0;
  bool found = false;
  for (std::size_t i = 0; i < eff.size(); ++i) {
    if (eff[i] == from) {
      idx = i;
      found = true;
      break;
    }
  }
  if (!found) return from;  // Unknown id: focus unchanged.
  const NodeId out = eff[(idx + eff.size() - 1) % eff.size()];
  focused_ = out;
  return out;
}

Status FocusTree::assignToGroup(NodeId node, GroupId group) {
  group_of_[node] = group;
  return Status::kOk;
}

Status FocusTree::enterGroup(GroupId group) {
  bool known = false;
  for (const auto& kv : group_of_) {
    if (kv.second != group) continue;
    for (NodeId id : order_) {
      if (id == kv.first) {
        known = true;
        break;
      }
    }
    if (known) break;
  }
  if (!known) return Status::kNotFound;
  trapped_ = true;
  active_group_ = group;
  const std::vector<NodeId> eff = EffectiveOrder();
  if (!eff.empty()) focused_ = eff[0];
  return Status::kOk;
}

Status FocusTree::exitGroup() {
  trapped_ = false;
  active_group_ = -1;
  return Status::kOk;
}

Status FocusTree::rebuild(Subtree subtree) {
  // Identical order: no-op, keep order_ and focused_ (no ring flash).
  if (subtree.nodes == order_) return Status::kOk;
  // Same multiset but different order: reject, keep prior order/focus.
  if (subtree.nodes.size() == order_.size()) {
    std::vector<NodeId> a = subtree.nodes;
    std::vector<NodeId> b = order_;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    if (a == b) return Status::kOrderChanged;
    // Same size but different members: not a rebuild of the same subtree.
    return Status::kOrderChanged;
  }
  // Subset: every rebuilt node existed before — adopt surviving order.
  bool is_subset = true;
  for (NodeId id : subtree.nodes) {
    bool found = false;
    for (NodeId cur : order_) {
      if (cur == id) {
        found = true;
        break;
      }
    }
    if (!found) {
      is_subset = false;
      break;
    }
  }
  if (!is_subset) return Status::kOrderChanged;
  order_ = std::move(subtree.nodes);
  if (order_.empty()) {
    focused_ = -1;
    return Status::kOk;
  }
  bool kept = false;
  for (NodeId id : order_) {
    if (id == focused_) {
      kept = true;
      break;
    }
  }
  if (!kept) focused_ = order_[0];
  return Status::kOk;
}

std::vector<A11yNode> FocusTree::exposeToA11y() const {
  std::vector<A11yNode> out;
  out.reserve(order_.size());
  for (NodeId id : order_) {
    A11yNode n;
    n.id = id;
    n.focused = (id == focused_);
    out.push_back(n);
  }
  return out;
}

std::vector<NodeId> FocusTree::EffectiveOrder() const {
  if (!trapped_) return order_;
  std::vector<NodeId> eff;
  eff.reserve(order_.size());
  for (NodeId id : order_) {
    auto it = group_of_.find(id);
    if (it != group_of_.end() && it->second == active_group_) eff.push_back(id);
  }
  return eff;
}

}  // namespace input
