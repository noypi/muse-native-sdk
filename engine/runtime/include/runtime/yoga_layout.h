#pragma once

#include <cstddef>
#include <vector>

#include "runtime/layout.h"
#include "runtime/tree.h"

namespace runtime {

// FR-DEPS-013 (FR-DEPS-008 D1): Yoga adapter behind the LayoutEngine
// seam. This header plus `engine/runtime/src/yoga_layout.cpp` are the ONLY
// production translation units permitted to name Yoga headers or YG*
// symbols (enforced by `tools/deps/seam_check.py`, TC-DEPS-049). Every
// other production target reaches Yoga exclusively through the
// composition-root binding in `runtime/layout_seam.h`. Test doubles stay in
// `tests/` and never link here.
//
// Mandatory posture: Yoga is the ONLY prod layout backend (fail-closed).
// The build always defines MUSE_HAVE_YOGA and links the T5 per-ABI
// yogacore static lib; a missing snapshot fails configure. YogaAvailable()
// is therefore always true on prod builds; YogaComputeBoxes() throws
// fail-closed (via the seam) instead of falling back. The in-tree
// flexbox-subset engine in `layout.cpp` remains only as the T8 parity
// reference (non-prod).

// Always true on prod builds (configure fails without the Yoga snapshot).
bool YogaAvailable();

// Solves `root` with Yoga and appends one LayoutBox per tree node id to
// `boxes` (same id scheme as LayoutEngine::boxOf). Returns true on success;
// failure throws fail-closed through the seam — the caller never falls back
// to the in-tree engine.
bool YogaComputeBoxes(const RuntimeTree& tree, NodeId root,
                      std::vector<LayoutBox>& boxes);

}  // namespace runtime
