// Batch 1 T2 contract tests — responsive primitives (FR-LAYOUT-011, AC-030).
// Hermetic: C++17 + responsive.h only; no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T2 `Interface:` line:
//   `LayoutResult measureResponsive(Node n, Constraints c) -> LayoutResult{size, baseline}; unsupported -> Status::kUnsupportedProperty`
//
// ASSUMPTION (struct shapes): the plan names `Node`, `Constraints`,
// `LayoutResult{size, baseline}` and `Status::kUnsupportedProperty` but not
// their full shapes. Tests assume:
//   `struct Constraints { float maxWidth; float maxHeight; };`
//   `struct Size { float width; float height; };`
//   `struct LayoutResult { Size size; float baseline; Status status; };`
//   `enum class Status { kOk, kUnsupportedProperty, kInvalidMetrics };`
//   `struct Node { ... };` exposing one focused factory per primitive:
//     `Node makeGrid(int columns, float cellW, float cellH, int childCount);`
//     `Node makeAspectBox(float ratio, float width);`
//     `Node makeWrapRow(float childW, float childH, int childCount);`
//     `Node makeBaselineBox(float width, float height, float baseline);`
//     `Node makeUnsupported(int bogusKind);`
//   `LayoutResult measureResponsive(Node n, Constraints c);`
// i.e. the status travels alongside the result (the Interface line names
// both `LayoutResult{...}` and `Status::kUnsupportedProperty`).
//
// ASSUMPTION (grid geometry): a 2-column grid of 100x50 cells with 4
// children measures exactly 200x100 under ample constraints.
//
// ASSUMPTION (aspect arithmetic): ratio means width/height, so width 160 at
// 16/9 gives height 90.
//
// ASSUMPTION (wrap geometry): three 100-wide children under maxWidth 250
// wrap to two lines (2 + 1), giving 200x100 for 100x50 children.
//
// ASSUMPTION (baseline contract): the baseline factory's declared baseline
// is reported verbatim and is within [0, height].
//
// ASSUMPTION (empty node): a default Node with no properties measures 0x0,
// baseline 0, status kOk.
//
// ASSUMPTION (malformed input): a negative aspect ratio is rejected with
// Status::kInvalidMetrics (never a throw across the boundary, never a
// silent fallback size).
#include <cstdio>

#include "responsive.h"

namespace {

// Ample constraints: nothing clips.
layout::Constraints ample() {
  layout::Constraints c;
  c.maxWidth = 2000.0f;
  c.maxHeight = 2000.0f;
  return c;
}

bool floatEq(float a, float b) { return a > b - 0.001f && a < b + 0.001f; }

}  // namespace

// Grid: 2 columns x 100x50 cells x 4 children -> 200x100.
static bool check_grid_measures_columns_times_cells() {
  layout::Node n = layout::makeGrid(2, 100.0f, 50.0f, 4);
  layout::LayoutResult r = layout::measureResponsive(n, ample());
  return r.status == layout::Status::kOk && floatEq(r.size.width, 200.0f) &&
         floatEq(r.size.height, 100.0f);
}

// Aspect ratio: width 160 at 16/9 -> height 90.
static bool check_aspect_ratio_preserves_height() {
  layout::Node n = layout::makeAspectBox(16.0f / 9.0f, 160.0f);
  layout::LayoutResult r = layout::measureResponsive(n, ample());
  return r.status == layout::Status::kOk && floatEq(r.size.width, 160.0f) &&
         floatEq(r.size.height, 90.0f);
}

// Wrap: three 100-wide children under maxWidth 250 -> two lines -> 200x100.
static bool check_wrap_flows_to_two_lines() {
  layout::Constraints c;
  c.maxWidth = 250.0f;
  c.maxHeight = 2000.0f;
  layout::Node n = layout::makeWrapRow(100.0f, 50.0f, 3);
  layout::LayoutResult r = layout::measureResponsive(n, c);
  return r.status == layout::Status::kOk && floatEq(r.size.width, 200.0f) &&
         floatEq(r.size.height, 100.0f);
}

// Baseline: declared baseline reported verbatim within [0, height].
static bool check_baseline_reported_verbatim() {
  layout::Node n = layout::makeBaselineBox(100.0f, 50.0f, 40.0f);
  layout::LayoutResult r = layout::measureResponsive(n, ample());
  return r.status == layout::Status::kOk && floatEq(r.baseline, 40.0f) &&
         r.baseline >= 0.0f && r.baseline <= r.size.height;
}

// Unsupported property errors: kUnsupportedProperty, never a silent no-op.
static bool check_unsupported_property_errors() {
  layout::Node n = layout::makeUnsupported(9999);
  bool threw = false;
  layout::LayoutResult r;
  try {
    r = layout::measureResponsive(n, ample());
  } catch (...) {
    threw = true;
  }
  if (threw) return false;  // module boundary must not throw.
  return r.status == layout::Status::kUnsupportedProperty;
}

// Empty node measures 0x0, baseline 0, kOk (ASSUMPTION).
static bool check_empty_node_measures_zero() {
  layout::Node n;
  layout::LayoutResult r = layout::measureResponsive(n, ample());
  return r.status == layout::Status::kOk && floatEq(r.size.width, 0.0f) &&
         floatEq(r.size.height, 0.0f) && floatEq(r.baseline, 0.0f);
}

// Malformed input (negative aspect ratio) is rejected, never silent.
static bool check_negative_aspect_ratio_is_rejected() {
  layout::Node n = layout::makeAspectBox(-1.0f, 160.0f);
  bool threw = false;
  layout::LayoutResult r;
  try {
    r = layout::measureResponsive(n, ample());
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  return r.status == layout::Status::kInvalidMetrics;
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
  RUN_CASE(check_grid_measures_columns_times_cells);
  RUN_CASE(check_aspect_ratio_preserves_height);
  RUN_CASE(check_wrap_flows_to_two_lines);
  RUN_CASE(check_baseline_reported_verbatim);
  RUN_CASE(check_unsupported_property_errors);
  RUN_CASE(check_empty_node_measures_zero);
  RUN_CASE(check_negative_aspect_ratio_is_rejected);
  if (failures > 0) {
    std::printf("responsive_primitive_test: %d failing\n", failures);
    return 1;
  }
  std::printf("responsive_primitive_test: all passing\n");
  return 0;
}
