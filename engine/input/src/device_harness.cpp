#include "input/device_harness.h"

namespace input {

std::vector<NodeId> DpadWalkOrder(FocusTree& tree) {
  // Ring order mirrors setOrder; exposeToA11y reflects the current order, so
  // walking it visits each focusable exactly once. Phone/tablet only: no TV
  // hardware paths exist here — D-pad steps stay inside the T2 tree.
  const std::vector<A11yNode> exposed = tree.exposeToA11y();
  std::vector<NodeId> visited;
  visited.reserve(exposed.size());
  for (const A11yNode& node : exposed) {
    visited.push_back(node.id);
  }
  return visited;
}

}  // namespace input
