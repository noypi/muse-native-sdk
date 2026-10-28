// Batch 2 T3 boundary tests — pane switch driven solely by the SizeClass
// boundary plus the consumed posture signal (FR-LAYOUT-011 AC-029).
// Hermetic: C++17 + adaptive_layout.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line:
//   `PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s, PostureSignal p) -> PaneArrangement{visiblePanes, hingeAvoidance}; unknown declaration -> Status::kUnknownLayout`
//
// Shape ASSUMPTIONs are identical to `adaptive_test.cpp` (see its header
// comment): `AdaptiveDecl{int layoutId}` with ids 1/2/3,
// `SizeClass{WidthClass, HeightClass}`, `PostureSignal{halfOpened,
// hingePresent}`, `PaneArrangement{primaryVisible, secondaryVisible,
// hingeAvoidance, status}`.
//
// ASSUMPTION (exact-boundary semantics): the switch happens exactly at the
// class transition — Compact renders single-pane, the next class up renders
// the wider arrangement. Tests pin classes directly (thresholds are T1's
// job), so "boundary" here means the tier step, not a dp value.
// ASSUMPTION (sole driver): the same SizeClass + same posture always yields
// the same arrangement (no hidden metric/cache input); posture is the only
// other consumed input.
// ASSUMPTION (unknown class): an Unknown width class renders the safe
// single-pane fallback with kOk rather than a two-pane guess.
//
// This TU includes ONLY `adaptive_layout.h` (never `size_class.h` or
// `responsive.h`): T1 and T2 each define their own `layout::Status` with
// different enumerators, so a TU including both fails to compile. See the
// contract note for the coder in T3.md.
#include <cstdio>

#include "adaptive_layout.h"

namespace {

layout::AdaptiveDecl decl(int id) {
  layout::AdaptiveDecl d;
  d.layoutId = id;
  return d;
}

layout::SizeClass sizeClass(layout::WidthClass w, layout::HeightClass h) {
  layout::SizeClass s;
  s.widthClass = w;
  s.heightClass = h;
  return s;
}

layout::PostureSignal flatPosture() {
  layout::PostureSignal p;
  p.halfOpened = false;
  p.hingePresent = false;
  return p;
}

layout::PostureSignal hingePosture() {
  layout::PostureSignal p;
  p.halfOpened = true;
  p.hingePresent = true;
  return p;
}

}  // namespace

// Compact/max-of-tier renders single-pane: the switch has not fired yet.
static bool check_compact_boundary_stays_single_pane() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  return r.status == layout::Status::kOk && r.primaryVisible &&
         !r.secondaryVisible && !r.hingeAvoidance;
}

// The compact->medium step switches list-detail to two panes: exactly one
// switch at this boundary (single below, dual at and above).
static bool check_medium_step_switches_list_detail_once() {
  auto below = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  auto at = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Medium, layout::HeightClass::Compact),
      flatPosture());
  auto above = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Compact),
      flatPosture());
  return !below.secondaryVisible && at.secondaryVisible &&
         above.secondaryVisible;
}

// Supporting-pane switches at the medium->expanded step only: Medium is
// still single-pane, Expanded adds the supporting pane.
static bool check_supporting_pane_switches_only_at_expanded() {
  auto medium = layout::arrangeAdaptive(
      decl(layout::kSupportingPaneId),
      sizeClass(layout::WidthClass::Medium, layout::HeightClass::Medium),
      flatPosture());
  auto expanded = layout::arrangeAdaptive(
      decl(layout::kSupportingPaneId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Medium),
      flatPosture());
  return medium.status == layout::Status::kOk && !medium.secondaryVisible &&
         expanded.status == layout::Status::kOk &&
         expanded.secondaryVisible;
}

// Sole-driver: identical SizeClass + identical posture yields identical
// arrangement on repeat calls (no hidden state, no metric re-read).
static bool check_same_class_same_posture_is_deterministic() {
  auto first = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Medium, layout::HeightClass::Medium),
      flatPosture());
  auto second = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Medium, layout::HeightClass::Medium),
      flatPosture());
  return first.status == second.status &&
         first.primaryVisible == second.primaryVisible &&
         first.secondaryVisible == second.secondaryVisible &&
         first.hingeAvoidance == second.hingeAvoidance;
}

// Unknown width class renders the safe single-pane fallback (ASSUMPTION).
static bool check_unknown_class_falls_back_to_single_pane() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Unknown, layout::HeightClass::Unknown),
      flatPosture());
  return r.status == layout::Status::kOk && r.primaryVisible &&
         !r.secondaryVisible;
}

// Fold sequence consumes the posture signal: compact/flat single-pane ->
// expanded/half-opened hinge-avoiding dual-pane -> back to compact/flat
// single-pane with no avoidance.
static bool check_fold_sequence_drives_panes_and_avoidance() {
  auto start = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  auto folded = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Medium),
      hingePosture());
  auto back = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  return !start.secondaryVisible && !start.hingeAvoidance &&
         folded.secondaryVisible && folded.hingeAvoidance &&
         !back.secondaryVisible && !back.hingeAvoidance;
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
  RUN_CASE(check_compact_boundary_stays_single_pane);
  RUN_CASE(check_medium_step_switches_list_detail_once);
  RUN_CASE(check_supporting_pane_switches_only_at_expanded);
  RUN_CASE(check_same_class_same_posture_is_deterministic);
  RUN_CASE(check_unknown_class_falls_back_to_single_pane);
  RUN_CASE(check_fold_sequence_drives_panes_and_avoidance);
  if (failures > 0) {
    std::printf("adaptive_boundary_test: %d failing\n", failures);
    return 1;
  }
  std::printf("adaptive_boundary_test: all passing\n");
  return 0;
}
