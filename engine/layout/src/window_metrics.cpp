#include "window_metrics.h"

#include <cmath>

namespace layout {
namespace {

// Breakpoints are injected thresholds (plan A2), pinned to the platform
// WindowSizeClass values so trigger expectations read in those terms.
Breakpoints triggerBreakpoints() noexcept {
  Breakpoints b;
  b.compactMaxWidth = 600.0f;
  b.mediumMaxWidth = 840.0f;
  return b;
}

// Recompute-and-store shared by the trigger entry points. Returns false
// when T1 rejects the metrics, leaving the stored class untouched.
bool recomputeFromMetrics(WindowMetrics m, SizeClass& stored) noexcept {
  SizeClassResult r = computeSizeClass(m, triggerBreakpoints());
  if (r.status != Status::kOk) {
    return false;
  }
  stored = r.size;
  return true;
}

SizeClass& storedClass() noexcept {
  static SizeClass current;
  return current;
}

int& recreationCount() noexcept {
  static int count = 0;
  return count;
}

// Last display whose metrics were consumed by a successful resize (T5,
// FR-LAYOUT-013 narrow slice: reflow + per-display metrics only). Updated
// only when onWindowBoundsChanged accepts both the bounds and the density;
// invalid input leaves the previously stored display untouched.
DisplayMetrics& storedDisplay() noexcept {
  static DisplayMetrics current{2.0f, 0};
  return current;
}

}  // namespace

Status onWindowLayoutInfo(WindowLayoutInfo info) noexcept {
  // Unparseable signal: flagged invalid, or metrics T1 rejects (zero,
  // negative, non-finite). Ignored with the prior class intact — never a
  // default class, never a recreation.
  if (!info.valid || !recomputeFromMetrics(info.metrics, storedClass())) {
    return Status::kIgnoredStaleInfo;
  }
  return Status::kOk;
}

SizeClass currentSizeClass() noexcept { return storedClass(); }

int recreationRequestCount() noexcept { return recreationCount(); }

// T5 in-place resize path (FR-LAYOUT-004 AC-LAYOUT-007, consuming
// FR-LIFECYCLE-003 in-place delivery and the FR-LAYOUT-013 per-display
// slice): reflows by recomputing the size class from the new bounds via T1
// and recording the new display's density/displayId, with no Activity
// recreation (recreationCount stays 0). Zero/negative/non-finite bounds or
// non-positive/non-finite density return kInvalidMetrics and leave the
// stored class and display untouched — never a default class.
Status onWindowBoundsChanged(WindowMetrics m, DisplayMetrics d) noexcept {
  if (!std::isfinite(d.density) || !(d.density > 0.0f) ||
      !recomputeFromMetrics(m, storedClass())) {
    return Status::kInvalidMetrics;
  }
  storedDisplay() = d;
  return Status::kOk;
}

Status requestRecreation() noexcept {
  return Status::kRecreationNotAllowed;
}

}  // namespace layout
