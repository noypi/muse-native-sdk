#pragma once

#include <cstddef>
#include <vector>

#include "runtime/layout.h"
#include "runtime/tree.h"

namespace runtime {

// FR-DEPS-013 T6 (FR-DEPS-008 D1): Yoga adapter behind the LayoutEngine
// seam. This header plus `engine/runtime/src/yoga_layout.cpp` are the ONLY
// production translation units permitted to name Yoga headers or YG*
// symbols (enforced by `tools/deps/seam_check.py`, TC-DEPS-049). Every
// other production target reaches Yoga exclusively through the
// composition-root binding in `runtime/layout_seam.h`. Test doubles stay in
// `tests/` and never link here.
//
// Interim posture (D1): until the vendored Yoga link lands (T5 cache rows),
// the in-tree flexbox-subset engine in `layout.cpp` remains the
// implementation. YogaAvailable() reports whether the Yoga headers were
// visible at build time; YogaComputeBoxes() returns false when they were
// not, so callers keep the in-tree path with zero behavior change.

// True when this TU was built against the vendored Yoga headers.
bool YogaAvailable();

// Solves `root` with Yoga and appends one LayoutBox per tree node id to
// `boxes` (same id scheme as LayoutEngine::boxOf). Returns true on success;
// returns false leaving `boxes` untouched when Yoga is not linked, so the
// caller falls back to the in-tree engine.
bool YogaComputeBoxes(const RuntimeTree& tree, NodeId root,
                      std::vector<LayoutBox>& boxes);

}  // namespace runtime
