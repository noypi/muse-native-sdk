#include "runtime/layout.h"

namespace runtime {

// T1 stub: full geometry resolution lands in T5.
LayoutBox LayoutEngine::compute(const RuntimeTree& /*tree*/, NodeId /*root*/) {
  root_box_ = LayoutBox{};
  has_root_ = true;
  return root_box_;
}

const LayoutBox& LayoutEngine::boxOf(NodeId /*id*/) const {
  if (!has_root_) {
    throw std::out_of_range("layout has not been computed");
  }
  return root_box_;
}

// T1 stub: dirty-subtree walk lands in T6.
std::size_t LayoutEngine::relayoutSubtree(RuntimeTree& /*tree*/,
                                          NodeId /*dirtyRoot*/) {
  return 0;
}

}  // namespace runtime
