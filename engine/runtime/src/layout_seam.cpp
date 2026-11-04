// FR-DEPS-013 (mandatory Yoga, fail-closed): composition-root LayoutEngine
// binding. Single production seam through which layout targets reach Yoga.
// This TU names only the adapter (yoga_layout.h), never Yoga headers
// directly — TC-DEPS-049 confinement (`tools/deps/seam_check.py`) enforces
// that. The prod path is Yoga-only: a Yoga solve failure throws
// fail-closed (callers already treat layout failure as fail-closed); the
// in-tree engine is never invoked here. It remains only as the T8 parity
// reference (non-prod).

#include "runtime/layout_seam.h"

#include <stdexcept>

#include "runtime/yoga_layout.h"

namespace runtime {

LayoutBackend ActiveLayoutBackend() { return LayoutBackend::kYoga; }

LayoutBox ComputeLayout(const RuntimeTree& tree, NodeId root,
                        LayoutEngine& engine, std::vector<LayoutBox>* boxes) {
  // Legacy parameter: retained for signature stability (callers + tests
  // construct a LayoutEngine). Never invoked on the prod path — the
  // in-tree engine is the T8 parity reference only, not a fallback.
  (void)engine;
  std::vector<LayoutBox> yogaBoxes;
  if (!YogaComputeBoxes(tree, root, yogaBoxes)) {
    throw std::runtime_error(
        "MUSE layout: Yoga solve failed (fail-closed: no in-tree fallback)");
  }
  const LayoutBox frame = yogaBoxes.at(static_cast<std::size_t>(root));
  if (boxes != nullptr) {
    *boxes = yogaBoxes;
  }
  return frame;
}

}  // namespace runtime
