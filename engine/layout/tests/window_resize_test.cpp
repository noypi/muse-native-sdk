// Batch 3 T4+T5 contract tests — fold/posture trigger + in-place resize
// (FR-LAYOUT-011 AC-028 trigger surface, FR-LAYOUT-004 AC-LAYOUT-007).
// Hermetic: C++17 + window_metrics.h only (size_class.h types arrive via that
// include, never responsive.h); no network, no env/config reads, no
// filesystem, no live services. Shared file owned by T4 per GR-08 T1.8,
// mapped in both T4.md and T5.md contracts.
//
// Binds to the plan `Interface:` lines (verbatim in contracts):
//   T4: `void onWindowLayoutInfo(WindowLayoutInfo info) -> Status;
//        unparseable info -> Status::kIgnoredStaleInfo, class unchanged`
//   T5: `Status onWindowBoundsChanged(WindowMetrics m, DisplayMetrics d) ->
//        ok | kInvalidMetrics; recreation requested ->
//        Status::kRecreationNotAllowed`
//
// ASSUMPTION (Status extension): size_class.h currently defines
//   `enum class Status { kOk, kInvalidMetrics, kUnsupportedProperty,
//                         kUnknownLayout }`.
// Tests require `Status::kIgnoredStaleInfo` and
// `Status::kRecreationNotAllowed` additions. The CODER extends size_class.h
// (tester never edits production code); if the coder names them differently,
// the contracts + call sites reconcile, behavior does not change.
//
// ASSUMPTION (T4 signature): the Interface line reads `void ... -> Status`;
// tests assume `Status onWindowLayoutInfo(WindowLayoutInfo info)` — the
// status travels as the return value (same convention as T1's SizeClassResult
// status and T5's Status return).
//
// ASSUMPTION (struct shapes): the plan names `WindowLayoutInfo`,
// `WindowMetrics`, `DisplayMetrics` but not their fields. Tests assume:
//   `struct WindowLayoutInfo { WindowMetrics metrics; bool valid; };`
//     (`valid=false`, or non-positive/non-finite metrics, = unparseable)
//   `struct DisplayMetrics { float density; int displayId; };`
//     (non-positive/non-finite density = invalid)
// WindowMetrics/Breakpoints/SizeClass/Status come from size_class.h.
//
// ASSUMPTION (observers): "class unchanged" and "no Activity recreation" need
// an observable. Tests assume:
//   `SizeClass currentSizeClass();` — last recomputed class (unchanged when
//     stale info is ignored / recreation is refused).
//   `int recreationRequestCount();` — number of performed recreations,
//     expected to stay 0 across every test (in-place only).
//
// ASSUMPTION (recreation entry point): the Interface line names the refusal
// status but no request spelling. Tests assume
//   `Status requestRecreation();`
// in window_metrics.h as the recreation-request entry point; it must return
// `Status::kRecreationNotAllowed` and perform no recreation. If the coder
// spells the request differently (e.g. a flag on onWindowBoundsChanged), the
// contract reconciles the call site; the refusal behavior stands.
//
// ASSUMPTION (thresholds): tests pin compactMaxWidth=600/mediumMaxWidth=840
// via the implementation's injected breakpoints (plan A2); expectations are
// stated in those terms (360->Compact, 700->Medium, 1200->Expanded).
#include <cstdio>

#include "window_metrics.h"

namespace {

layout::WindowMetrics metrics(float w, float h) {
  layout::WindowMetrics m;
  m.width = w;
  m.height = h;
  return m;
}

layout::WindowLayoutInfo info(float w, float h, bool valid = true) {
  layout::WindowLayoutInfo i;
  i.metrics = metrics(w, h);
  i.valid = valid;
  return i;
}

layout::DisplayMetrics display(float density = 2.0f, int id = 0) {
  layout::DisplayMetrics d;
  d.density = density;
  d.displayId = id;
  return d;
}

}  // namespace

// --- T4: fold/posture trigger surface (FR-LAYOUT-011 AC-028) ---

// Valid fold/unfold notification recomputes the size class via T1.
static bool check_valid_layout_info_recomputes_class() {
  int recreationsBefore = layout::recreationRequestCount();
  if (layout::onWindowLayoutInfo(info(360.0f, 640.0f)) !=
      layout::Status::kOk) {
    return false;
  }
  if (layout::currentSizeClass().widthClass != layout::WidthClass::Compact) {
    return false;
  }
  if (layout::onWindowLayoutInfo(info(1200.0f, 800.0f)) !=
      layout::Status::kOk) {
    return false;
  }
  return layout::currentSizeClass().widthClass ==
             layout::WidthClass::Expanded &&
         layout::recreationRequestCount() == recreationsBefore;
}

// Fold/unfold/refold sequence retriggers every time with no stale cache and
// no recreation (FR-LAYOUT-014 trigger-only: signal consumed, not
// reimplemented; FR-LAYOUT-002/014 splitting stays out of scope).
static bool check_fold_sequence_retriggers_without_recreation() {
  int recreationsBefore = layout::recreationRequestCount();
  layout::onWindowLayoutInfo(info(360.0f, 640.0f));
  layout::onWindowLayoutInfo(info(1100.0f, 700.0f));
  bool unfoldedExpanded = layout::currentSizeClass().widthClass ==
                          layout::WidthClass::Expanded;
  layout::onWindowLayoutInfo(info(360.0f, 640.0f));
  return unfoldedExpanded &&
         layout::currentSizeClass().widthClass ==
             layout::WidthClass::Compact &&
         layout::recreationRequestCount() == recreationsBefore;
}

// Unparseable info (valid=false) is ignored: kIgnoredStaleInfo, class intact.
static bool check_stale_info_ignored_class_unchanged() {
  layout::onWindowLayoutInfo(info(700.0f, 640.0f));  // Medium baseline
  if (layout::currentSizeClass().widthClass != layout::WidthClass::Medium) {
    return false;
  }
  if (layout::onWindowLayoutInfo(info(1200.0f, 800.0f, false)) !=
      layout::Status::kIgnoredStaleInfo) {
    return false;
  }
  return layout::currentSizeClass().widthClass == layout::WidthClass::Medium;
}

// Garbage metrics inside layout info are stale too, never a default class.
static bool check_garbage_metrics_info_ignored() {
  layout::onWindowLayoutInfo(info(700.0f, 640.0f));  // Medium baseline
  if (layout::onWindowLayoutInfo(info(0.0f, 0.0f)) !=
      layout::Status::kIgnoredStaleInfo) {
    return false;
  }
  if (layout::currentSizeClass().widthClass != layout::WidthClass::Medium) {
    return false;
  }
  return layout::onWindowLayoutInfo(info(-50.0f, 640.0f)) ==
         layout::Status::kIgnoredStaleInfo;
}

// Boundary trigger: metrics exactly at compactMax stay Compact (tie rule
// shared with T1), still kOk — the trigger adds no bookkeeping of its own.
static bool check_boundary_info_triggers_boundary_class() {
  if (layout::onWindowLayoutInfo(info(600.0f, 640.0f)) != layout::Status::kOk) {
    return false;
  }
  return layout::currentSizeClass().widthClass == layout::WidthClass::Compact;
}

// --- T5: in-place resize path (FR-LAYOUT-004 AC-LAYOUT-007) ---

// Continuous resize reflows in place: every step kOk, class tracks the new
// bounds, recreation count stays 0 (FR-LIFECYCLE-003 consumed, not
// implemented — no delivery machinery here).
static bool check_continuous_resize_reflows_in_place() {
  int recreationsBefore = layout::recreationRequestCount();
  const float widths[] = {360.0f, 500.0f, 700.0f, 900.0f, 1200.0f};
  for (float w : widths) {
    if (layout::onWindowBoundsChanged(metrics(w, 700.0f), display()) !=
        layout::Status::kOk) {
      return false;
    }
  }
  return layout::currentSizeClass().widthClass ==
             layout::WidthClass::Expanded &&
         layout::recreationRequestCount() == recreationsBefore;
}

// Cross-display move re-lays out with the new display's density/metrics
// (FR-LAYOUT-013 narrow slice: reflow + per-display metrics only; AC-038
// cross-window drag stays out of scope).
static bool check_cross_display_move_relayouts_with_new_density() {
  int recreationsBefore = layout::recreationRequestCount();
  if (layout::onWindowBoundsChanged(metrics(700.0f, 640.0f),
                                    display(2.0f, 0)) != layout::Status::kOk) {
    return false;
  }
  if (layout::onWindowBoundsChanged(metrics(700.0f, 640.0f),
                                    display(3.0f, 1)) != layout::Status::kOk) {
    return false;
  }
  return layout::currentSizeClass().widthClass == layout::WidthClass::Medium &&
         layout::recreationRequestCount() == recreationsBefore;
}

// Zero bounds are invalid, never a default class.
static bool check_zero_bounds_yield_invalid() {
  return layout::onWindowBoundsChanged(metrics(0.0f, 0.0f), display()) ==
         layout::Status::kInvalidMetrics;
}

// Negative bounds are invalid.
static bool check_negative_bounds_yield_invalid() {
  return layout::onWindowBoundsChanged(metrics(-10.0f, 640.0f), display()) ==
         layout::Status::kInvalidMetrics;
}

// Invalid display metrics (zero density) are invalid too (ASSUMPTION:
// density must be positive and finite).
static bool check_invalid_display_density_yield_invalid() {
  return layout::onWindowBoundsChanged(metrics(700.0f, 640.0f),
                                       display(0.0f, 0)) ==
         layout::Status::kInvalidMetrics;
}

// Any recreation request is refused and performs nothing.
static bool check_recreation_request_refused() {
  layout::onWindowBoundsChanged(metrics(700.0f, 640.0f), display());
  int recreationsBefore = layout::recreationRequestCount();
  if (layout::requestRecreation() !=
      layout::Status::kRecreationNotAllowed) {
    return false;
  }
  return layout::recreationRequestCount() == recreationsBefore &&
         layout::currentSizeClass().widthClass == layout::WidthClass::Medium;
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
  RUN_CASE(check_valid_layout_info_recomputes_class);
  RUN_CASE(check_fold_sequence_retriggers_without_recreation);
  RUN_CASE(check_stale_info_ignored_class_unchanged);
  RUN_CASE(check_garbage_metrics_info_ignored);
  RUN_CASE(check_boundary_info_triggers_boundary_class);
  RUN_CASE(check_continuous_resize_reflows_in_place);
  RUN_CASE(check_cross_display_move_relayouts_with_new_density);
  RUN_CASE(check_zero_bounds_yield_invalid);
  RUN_CASE(check_negative_bounds_yield_invalid);
  RUN_CASE(check_invalid_display_density_yield_invalid);
  RUN_CASE(check_recreation_request_refused);
  if (failures > 0) {
    std::printf("window_resize_test: %d failing\n", failures);
    return 1;
  }
  std::printf("window_resize_test: all passing\n");
  return 0;
}
