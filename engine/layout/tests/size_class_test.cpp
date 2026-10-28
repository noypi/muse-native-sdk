// Batch 1 T1 contract tests — window size classes (FR-LAYOUT-011, AC-028).
// Hermetic: C++17 + size_class.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T1 `Interface:` line:
//   `SizeClass computeSizeClass(WindowMetrics m, Breakpoints b) -> SizeClass{widthClass,heightClass}; invalid metrics -> WidthClass::Unknown + Status::kInvalidMetrics`
//
// ASSUMPTION (struct shapes): the plan names `WindowMetrics`, `Breakpoints`,
// `SizeClass{widthClass,heightClass}`, `WidthClass::Unknown` and
// `Status::kInvalidMetrics` but not their full shapes. Tests assume:
//   `struct WindowMetrics { float width; float height; };` (dp)
//   `struct Breakpoints { float compactMaxWidth; float mediumMaxWidth;
//                          float compactMaxHeight; float mediumMaxHeight; };`
//   `enum class WidthClass { Unknown, Compact, Medium, Expanded };`
//   `enum class HeightClass { Unknown, Compact, Medium, Expanded };`
//   `enum class Status { kOk, kInvalidMetrics };`
//   `struct SizeClass { WidthClass widthClass; HeightClass heightClass; };`
//   `struct SizeClassResult { SizeClass size; Status status; };`
//   `SizeClassResult computeSizeClass(WindowMetrics m, Breakpoints b);`
// i.e. thresholds are injected per call (plan A2), never read from the
// platform, and the status travels alongside the size (the Interface line
// names both `SizeClass{...}` and `Status::kInvalidMetrics`).
//
// ASSUMPTION (pinned thresholds): tests pin compactMaxWidth=600,
// mediumMaxWidth=840, compactMaxHeight=480, mediumMaxHeight=900 per plan A2
// (breakpoints injected so tests are deterministic, not platform-read).
//
// ASSUMPTION (boundary rule): a metric exactly equal to a tier max stays in
// the lower tier (compactMax -> Compact, mediumMax -> Medium).
//
// ASSUMPTION (stateless recompute): AC-028 "recomputed on resize/fold ...
// with no cached Activity state" is tested as pure-function statelessness:
// repeated calls with different metrics each return the class for their own
// input, and a compact->expanded->compact sequence ends back at Compact.
#include <cstdio>

#include "size_class.h"

namespace {

// Pinned injected thresholds (see ASSUMPTION above).
layout::Breakpoints pinnedBreakpoints() {
  layout::Breakpoints b;
  b.compactMaxWidth = 600.0f;
  b.mediumMaxWidth = 840.0f;
  b.compactMaxHeight = 480.0f;
  b.mediumMaxHeight = 900.0f;
  return b;
}

layout::WindowMetrics metrics(float w, float h) {
  layout::WindowMetrics m;
  m.width = w;
  m.height = h;
  return m;
}

}  // namespace

// Compact phone portrait classifies Compact/Compact.
static bool check_compact_metrics_yield_compact_compact() {
  auto r = layout::computeSizeClass(metrics(360.0f, 400.0f), pinnedBreakpoints());
  return r.status == layout::Status::kOk &&
         r.size.widthClass == layout::WidthClass::Compact &&
         r.size.heightClass == layout::HeightClass::Compact;
}

// Medium width (tablet portrait / unfolded inner) classifies Medium.
static bool check_medium_width_yields_medium() {
  auto r = layout::computeSizeClass(metrics(700.0f, 640.0f), pinnedBreakpoints());
  return r.status == layout::Status::kOk &&
         r.size.widthClass == layout::WidthClass::Medium;
}

// Expanded width (desktop / wide tablet) classifies Expanded.
static bool check_expanded_width_yields_expanded() {
  auto r = layout::computeSizeClass(metrics(1200.0f, 800.0f), pinnedBreakpoints());
  return r.status == layout::Status::kOk &&
         r.size.widthClass == layout::WidthClass::Expanded &&
         r.size.heightClass == layout::HeightClass::Medium;
}

// Expanded height (tall window) classifies Expanded.
static bool check_expanded_height_yields_expanded() {
  auto r = layout::computeSizeClass(metrics(360.0f, 1000.0f), pinnedBreakpoints());
  return r.status == layout::Status::kOk &&
         r.size.heightClass == layout::HeightClass::Expanded;
}

// Resize recompute: a second call with new metrics returns the new class
// (AC-028 — recomputed on resize, no recreation, no cached Activity state).
static bool check_recompute_on_resize_returns_new_class() {
  auto b = pinnedBreakpoints();
  auto before = layout::computeSizeClass(metrics(360.0f, 640.0f), b);
  auto after = layout::computeSizeClass(metrics(1200.0f, 800.0f), b);
  return before.size.widthClass == layout::WidthClass::Compact &&
         after.size.widthClass == layout::WidthClass::Expanded &&
         after.status == layout::Status::kOk;
}

// Fold/unfold sequence recomputes every time with no stale cache.
static bool check_fold_sequence_has_no_stale_cache() {
  auto b = pinnedBreakpoints();
  auto folded = layout::computeSizeClass(metrics(360.0f, 640.0f), b);
  auto unfolded = layout::computeSizeClass(metrics(1100.0f, 700.0f), b);
  auto refolded = layout::computeSizeClass(metrics(360.0f, 640.0f), b);
  return folded.size.widthClass == layout::WidthClass::Compact &&
         unfolded.size.widthClass == layout::WidthClass::Expanded &&
         refolded.size.widthClass == layout::WidthClass::Compact;
}

// Zero metrics are invalid: Unknown + kInvalidMetrics, never a default class.
static bool check_zero_metrics_yield_unknown_invalid() {
  auto r = layout::computeSizeClass(metrics(0.0f, 0.0f), pinnedBreakpoints());
  return r.status == layout::Status::kInvalidMetrics &&
         r.size.widthClass == layout::WidthClass::Unknown;
}

// Negative metrics are invalid.
static bool check_negative_metrics_yield_invalid() {
  auto r = layout::computeSizeClass(metrics(-10.0f, 640.0f), pinnedBreakpoints());
  return r.status == layout::Status::kInvalidMetrics &&
         r.size.widthClass == layout::WidthClass::Unknown;
}

// Unknown (zero) height is invalid even when width is valid.
static bool check_zero_height_yields_invalid() {
  auto r = layout::computeSizeClass(metrics(360.0f, 0.0f), pinnedBreakpoints());
  return r.status == layout::Status::kInvalidMetrics;
}

// Boundary: exactly compactMaxWidth stays Compact (ASSUMPTION).
static bool check_compact_max_boundary_stays_compact() {
  auto r = layout::computeSizeClass(metrics(600.0f, 640.0f), pinnedBreakpoints());
  return r.status == layout::Status::kOk &&
         r.size.widthClass == layout::WidthClass::Compact;
}

// Boundary: exactly mediumMaxWidth stays Medium (ASSUMPTION).
static bool check_medium_max_boundary_stays_medium() {
  auto r = layout::computeSizeClass(metrics(840.0f, 640.0f), pinnedBreakpoints());
  return r.status == layout::Status::kOk &&
         r.size.widthClass == layout::WidthClass::Medium;
}

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_compact_metrics_yield_compact_compact);
  RUN_CASE(check_medium_width_yields_medium);
  RUN_CASE(check_expanded_width_yields_expanded);
  RUN_CASE(check_expanded_height_yields_expanded);
  RUN_CASE(check_recompute_on_resize_returns_new_class);
  RUN_CASE(check_fold_sequence_has_no_stale_cache);
  RUN_CASE(check_zero_metrics_yield_unknown_invalid);
  RUN_CASE(check_negative_metrics_yield_invalid);
  RUN_CASE(check_zero_height_yields_invalid);
  RUN_CASE(check_compact_max_boundary_stays_compact);
  RUN_CASE(check_medium_max_boundary_stays_medium);
  if (failures > 0) {
    std::printf("size_class_test: %d failing\n", failures);
    return 1;
  }
  std::printf("size_class_test: all passing\n");
  return 0;
}
