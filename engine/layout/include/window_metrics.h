#pragma once

#include "size_class.h"

namespace layout {

// Consumed fold/posture signal (interface only per FR-LAYOUT-002/014):
// the platform WindowLayoutInfo notification arrives here as a trigger.
// `valid=false`, or non-positive/non-finite metrics inside, means the info
// is unparseable and must be ignored with the prior class intact.
struct WindowLayoutInfo {
  WindowMetrics metrics;
  bool valid = true;
};

// Per-display metrics consumed on window-bounds changes (FR-LAYOUT-013
// narrow slice: reflow + per-display metrics only). Non-positive or
// non-finite density is invalid.
// NOTE (narrow-slice density semantics): WindowMetrics arrive in dp and the
// T1 breakpoints are dp thresholds, so density/displayId have no geometric
// effect on the recompute path in this slice — density is validated (invalid
// values reject with kInvalidMetrics) and the accepted display is recorded
// for cross-display identity, but the size class derives solely from the dp
// bounds. Any future px-to-dp scaling must come with a spec update.
struct DisplayMetrics {
  float density = 0.0f;
  int displayId = 0;
};

// Fold/posture notification trigger (T4, FR-LAYOUT-011 AC-028): recomputes
// the size class via T1's computeSizeClass against injected breakpoints
// (plan A2) and stores it for currentSizeClass(). Consumes the signal only:
// no Activity recreation, no app-level bookkeeping, no stale cache — every
// valid notification recomputes from its own metrics.
// Unparseable info (valid=false, or metrics T1 rejects) returns
// Status::kIgnoredStaleInfo and leaves the stored class unchanged.
Status onWindowLayoutInfo(WindowLayoutInfo info) noexcept;

// Last recomputed size class. Unknown/Unknown until the first valid
// notification or bounds change; unchanged whenever stale info is ignored
// or recreation is refused.
SizeClass currentSizeClass() noexcept;

// Number of performed Activity recreations. Always 0: resize and fold
// handling reflow in place and never recreate.
int recreationRequestCount() noexcept;

// In-place resize path (T5, FR-LAYOUT-004 AC-LAYOUT-007). Declared here so
// the shared window_resize_test.cpp compiles; behavior belongs to T5.
Status onWindowBoundsChanged(WindowMetrics m, DisplayMetrics d) noexcept;

// Recreation-request entry point. Always refused: returns
// Status::kRecreationNotAllowed and performs no recreation.
Status requestRecreation() noexcept;

}  // namespace layout
