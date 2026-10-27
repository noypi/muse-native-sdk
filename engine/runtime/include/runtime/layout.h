#pragma once

#include <cstddef>

#include "runtime/tree.h"

namespace runtime {

struct LayoutBox {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// Minimal in-repo flexbox-subset engine (plan A2); a future Yoga adapter
// sits behind this seam without changing the test-expectation shape.
class LayoutEngine {
 public:
  LayoutEngine() = default;

  // Resolves geometry for every node under `root`; returns the root frame.
  LayoutBox compute(const RuntimeTree& tree, NodeId root);

  const LayoutBox& boxOf(NodeId id) const;

  // Re-lays out only the dirty subtree; returns the number of nodes
  // touched. Ancestors' frames are adjusted without re-entering clean
  // subtrees.
  std::size_t relayoutSubtree(RuntimeTree& tree, NodeId dirtyRoot);

 private:
  LayoutBox root_box_;
  bool has_root_ = false;
};

}  // namespace runtime
