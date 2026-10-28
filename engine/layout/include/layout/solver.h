#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "layout/types.h"

namespace layout {

// Forward declaration; defined in layout/delegate.h (T3). The Solver stores
// only a non-owning raw pointer (caller owns the delegate lifetime).
class LayoutDelegate;

// Hybrid tree backend tag. Unknown values (e.g. from
// static_cast<Backend>(999)) are rejected with an exception.
enum class Backend { Flex, Constraint };

// Linear pin on one box channel: attribute 0=x, 1=y, 2=w, 3=h.
// `constant` is the target value; `weight` is the stay weight used to
// order conflicting pins (stronger weights win).
struct Constraint {
  NodeId node = kInvalidNodeId;
  int attribute = 0;
  float constant = 0.0f;
  float weight = 1.0f;
};

// Plain C++ frame (NFR-001: no JNI types on this path).
struct Box {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

class Solver {
 public:
  Solver() = default;

  NodeId addNode(Backend b, NodeId parent);
  void setFlex(NodeId id, float w, float h, float flexGrow);
  void addConstraint(const Constraint& c);
  Box solve(NodeId root);
  const Box& boxOf(NodeId id) const;

  // T3 delegate registry (free functions in layout/delegate.h). The Solver
  // holds non-owning raw pointers only; it never deletes a delegate.
  friend bool registerDelegate(Solver& s, NodeId subtreeRoot,
                               LayoutDelegate* d) noexcept;
  friend void unregisterDelegate(Solver& s, NodeId subtreeRoot) noexcept;

 private:
  struct NodeRec {
    Backend backend = Backend::Flex;
    NodeId parent = kInvalidNodeId;
    float w = 0.0f;
    float h = 0.0f;
    float flexGrow = 0.0f;
  };

  void layoutFlexSubtree(NodeId id);
  bool isInSubtree(NodeId node, NodeId root) const noexcept;
  bool isHandled(NodeId id) const;
  void dispatchDelegates(NodeId root);

  std::vector<NodeRec> nodes_;
  std::vector<Box> boxes_;
  std::vector<Constraint> constraints_;
  std::vector<std::pair<NodeId, LayoutDelegate*>> delegates_;
  bool solved_ = false;
};

}  // namespace layout
