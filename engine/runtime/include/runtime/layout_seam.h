#pragma once

#include <vector>

#include "runtime/layout.h"
#include "runtime/tree.h"

namespace runtime {

// FR-DEPS-013 T6 (AC-DEPS-049): composition-root LayoutEngine binding. This
// is the SINGLE production seam through which layout targets reach Yoga:
// callers use ComputeLayout() instead of touching Yoga headers or YG*
// symbols (both are confined to yoga_layout.{h,cpp} per TC-DEPS-049). Test
// doubles stay in tests/ and never route through here.

enum class LayoutBackend {
  kInTree,  // T8 parity reference only (non-prod). Never returned on the
            // prod path; retained so parity harnesses can name the baseline.
  kYoga,    // Vendored Yoga snapshot linked (mandatory, fail-closed).
};

// Always kYoga on prod builds (fail-closed: configure rejects any
// non-Yoga posture, so there is no backend to select).
LayoutBackend ActiveLayoutBackend();

// Solves `root` with Yoga and fills `*boxes` with one LayoutBox per tree
// node id (same id scheme as LayoutEngine::boxOf); returns the root frame.
// Yoga-only: a solve failure throws fail-closed, never the in-tree `engine`
// path. `engine` is a legacy signature parameter (parity reference only).
// `boxes` may be null to skip the copy (root frame only).
LayoutBox ComputeLayout(const RuntimeTree& tree, NodeId root,
                        LayoutEngine& engine, std::vector<LayoutBox>* boxes);

}  // namespace runtime
