#pragma once

#include <cstdint>

#include "layout/types.h"

namespace layout {

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

 private:
  struct NodeRec {
    Backend backend = Backend::Flex;
    NodeId parent = kInvalidNodeId;
    float w = 0.0f;
    float h = 0.0f;
    float flexGrow = 0.0f;
  };

  void layoutFlexSubtree(NodeId id);

  std::vector<NodeRec> nodes_;
  std::vector<Box> boxes_;
  std::vector<Constraint> constraints_;
  bool solved_ = false;
};

}  // namespace layout
