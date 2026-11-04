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
  kInTree,  // Interim (FR-DEPS-008 D1): in-tree flexbox-subset engine.
  kYoga,    // Vendored Yoga snapshot linked (T5 cache rows landed).
};

// Reports which backend ComputeLayout() will select: kYoga exactly when
// the Yoga headers were visible at build time, kInTree otherwise.
LayoutBackend ActiveLayoutBackend();

// Solves `root` and fills `*boxes` with one LayoutBox per tree node id (same
// id scheme as LayoutEngine::boxOf); returns the root frame. Yoga path when
// linked, in-tree `engine` path otherwise — behavior is identical to
// LayoutEngine::compute until the Yoga link lands. `boxes` may be null to
// skip the copy (root frame only).
LayoutBox ComputeLayout(const RuntimeTree& tree, NodeId root,
                        LayoutEngine& engine, std::vector<LayoutBox>* boxes);

}  // namespace runtime
