// Batch 2 T3 contract tests — declarative canonical adaptive layouts
// (FR-LAYOUT-003 AC-LAYOUT-006; TC-LAYOUT-006 gate via `./gradlew :layout:adaptiveTest`).
// Hermetic: C++17 + adaptive_layout.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T3 `Interface:` line:
//   `PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s, PostureSignal p) -> PaneArrangement{visiblePanes, hingeAvoidance}; unknown declaration -> Status::kUnknownLayout`
//
// ASSUMPTION (shapes): the plan names `AdaptiveDecl`, `SizeClass`,
// `PostureSignal`, `PaneArrangement{visiblePanes, hingeAvoidance}` and
// `Status::kUnknownLayout` but not their full shapes. Tests assume:
//   `struct AdaptiveDecl { int layoutId; };`
//     with `layout::kListDetailId == 1`, `layout::kSupportingPaneId == 2`,
//     `layout::kFeedId == 3`; any other id is an unknown declaration.
//   `struct SizeClass { WidthClass widthClass; HeightClass heightClass; };`
//     with `WidthClass/HeightClass ∈ {Unknown, Compact, Medium, Expanded}`
//     (same tier names as T1 `size_class.h`; thresholds are T1's job —
//     T3 takes the already-computed class, never raw metrics).
//   `struct PostureSignal { bool halfOpened; bool hingePresent; };`
//     (consumed fold signal; flat/no-hinge is `{false, false}`).
//   `struct PaneArrangement { bool primaryVisible; bool secondaryVisible;
//                             bool hingeAvoidance; Status status; };`
//     i.e. `visiblePanes` is the primary/secondary visibility pair and the
//     status travels on the arrangement (the Interface line names both the
//     panes and the status but not the carrier).
//   `PaneArrangement arrangeAdaptive(AdaptiveDecl d, SizeClass s,
//                                    PostureSignal p) noexcept;`
//
// ASSUMPTION (canonical semantics): list-detail shows the list alone on
// Compact and list+detail on Medium/Expanded; supporting-pane shows a single
// pane below Expanded and adds the supporting pane on Expanded; feed shows a
// single pane at every class. A half-opened posture with a present hinge
// sets `hingeAvoidance`; flat with no hinge never does.
//
// This TU includes ONLY `adaptive_layout.h` (never `size_class.h` or
// `responsive.h`): T1 and T2 each define their own `layout::Status` with
// different enumerators, so a TU including both fails to compile. The
// contract note for the coder (T3.md) asks for one unified status enum.
// If the Interface line's error shape needs the wider enum, that is recorded
// as an ASSUMPTION there, not invented behavior here.
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

// List-detail on Compact shows the list alone.
static bool check_list_detail_compact_shows_list_only() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  return r.status == layout::Status::kOk && r.primaryVisible &&
         !r.secondaryVisible;
}

// List-detail on Medium shows list + detail.
static bool check_list_detail_medium_shows_both_panes() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Medium, layout::HeightClass::Medium),
      flatPosture());
  return r.status == layout::Status::kOk && r.primaryVisible &&
         r.secondaryVisible;
}

// List-detail on Expanded shows list + detail.
static bool check_list_detail_expanded_shows_both_panes() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Expanded),
      flatPosture());
  return r.status == layout::Status::kOk && r.primaryVisible &&
         r.secondaryVisible;
}

// Supporting-pane is single-pane on Compact, two-pane on Expanded.
static bool check_supporting_pane_compact_single_expanded_dual() {
  auto compact = layout::arrangeAdaptive(
      decl(layout::kSupportingPaneId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  auto expanded = layout::arrangeAdaptive(
      decl(layout::kSupportingPaneId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Expanded),
      flatPosture());
  return compact.status == layout::Status::kOk && compact.primaryVisible &&
         !compact.secondaryVisible && expanded.status == layout::Status::kOk &&
         expanded.primaryVisible && expanded.secondaryVisible;
}

// Feed is a single pane at every class (class-independent).
static bool check_feed_single_pane_at_all_classes() {
  auto compact = layout::arrangeAdaptive(
      decl(layout::kFeedId),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  auto expanded = layout::arrangeAdaptive(
      decl(layout::kFeedId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Expanded),
      flatPosture());
  return compact.status == layout::Status::kOk && compact.primaryVisible &&
         !compact.secondaryVisible && expanded.status == layout::Status::kOk &&
         expanded.primaryVisible && !expanded.secondaryVisible;
}

// Unknown layout id errors with kUnknownLayout instead of a silent fallback.
static bool check_unknown_declaration_yields_unknown_layout() {
  auto compact = layout::arrangeAdaptive(
      decl(999),
      sizeClass(layout::WidthClass::Compact, layout::HeightClass::Compact),
      flatPosture());
  auto expanded = layout::arrangeAdaptive(
      decl(999),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Expanded),
      flatPosture());
  return compact.status == layout::Status::kUnknownLayout &&
         expanded.status == layout::Status::kUnknownLayout;
}

// Half-opened posture with a present hinge sets hingeAvoidance.
static bool check_half_opened_hinge_sets_hinge_avoidance() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Medium),
      hingePosture());
  return r.status == layout::Status::kOk && r.hingeAvoidance;
}

// Flat posture with no hinge never sets hingeAvoidance.
static bool check_flat_posture_has_no_hinge_avoidance() {
  auto r = layout::arrangeAdaptive(
      decl(layout::kListDetailId),
      sizeClass(layout::WidthClass::Expanded, layout::HeightClass::Medium),
      flatPosture());
  return r.status == layout::Status::kOk && !r.hingeAvoidance;
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
  RUN_CASE(check_list_detail_compact_shows_list_only);
  RUN_CASE(check_list_detail_medium_shows_both_panes);
  RUN_CASE(check_list_detail_expanded_shows_both_panes);
  RUN_CASE(check_supporting_pane_compact_single_expanded_dual);
  RUN_CASE(check_feed_single_pane_at_all_classes);
  RUN_CASE(check_unknown_declaration_yields_unknown_layout);
  RUN_CASE(check_half_opened_hinge_sets_hinge_avoidance);
  RUN_CASE(check_flat_posture_has_no_hinge_avoidance);
  if (failures > 0) {
    std::printf("adaptive_test: %d failing\n", failures);
    return 1;
  }
  std::printf("adaptive_test: all passing\n");
  return 0;
}
