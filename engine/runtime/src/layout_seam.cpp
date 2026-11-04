// FR-DEPS-013 T6 (AC-DEPS-049): composition-root LayoutEngine binding.
// Single production seam through which layout targets reach Yoga. This TU
// names only the adapter (yoga_layout.h), never Yoga headers directly —
// TC-DEPS-049 confinement (`tools/deps/seam_check.py`) enforces that.

#include "runtime/layout_seam.h"

#include "runtime/yoga_layout.h"

namespace runtime {

LayoutBackend ActiveLayoutBackend() {
  return YogaAvailable() ? LayoutBackend::kYoga : LayoutBackend::kInTree;
}

LayoutBox ComputeLayout(const RuntimeTree& tree, NodeId root,
                        LayoutEngine& engine, std::vector<LayoutBox>* boxes) {
  std::vector<LayoutBox> yogaBoxes;
  if (YogaComputeBoxes(tree, root, yogaBoxes)) {
    const LayoutBox frame = yogaBoxes.at(static_cast<std::size_t>(root));
    if (boxes != nullptr) {
      *boxes = yogaBoxes;
    }
    return frame;
  }
  // Interim path (FR-DEPS-008 D1): in-tree engine owns the frames.
  const LayoutBox frame = engine.compute(tree, root);
  if (boxes != nullptr) {
    boxes->clear();
    boxes->resize(tree.size());
    for (std::size_t i = 0; i < tree.size(); ++i) {
      (*boxes)[i] = engine.boxOf(static_cast<NodeId>(i));
    }
  }
  return frame;
}

}  // namespace runtime
