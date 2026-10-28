#pragma once

#include "size_class.h"

namespace layout {

// Status is the unified layout::Status from size_class.h (kOk,
// kInvalidMetrics, kUnsupportedProperty, kUnknownLayout, kIgnoredStaleInfo,
// kRecreationNotAllowed). responsive.h reuses it so any TU can include
// size_class.h + responsive.h (+ adaptive_layout.h + window_metrics.h)
// together. The T2 `Interface:` line names both
// `LayoutResult{size, baseline}` and `Status::kUnsupportedProperty`. The
// boundary never throws; unsupported combinations report kUnsupportedProperty
// and malformed inputs report kInvalidMetrics.
struct Constraints {
  float maxWidth = 0.0f;
  float maxHeight = 0.0f;
};

struct Size {
  float width = 0.0f;
  float height = 0.0f;
};

struct LayoutResult {
  Size size;
  float baseline = 0.0f;
  Status status = Status::kOk;
};

// First-class responsive primitive descriptor. A default-constructed Node is
// the empty node (measures 0x0, baseline 0, kOk).
struct Node {
  enum class Kind {
    kEmpty,
    kGrid,
    kAspectBox,
    kWrapRow,
    kBaselineBox,
    kUnsupported,
  };

  Kind kind = Kind::kEmpty;
  // Grid: columns x (cellW x cellH) x childCount.
  int columns = 0;
  float cellW = 0.0f;
  float cellH = 0.0f;
  int childCount = 0;
  // Aspect box: ratio is width/height; width pins the measured width.
  float ratio = 0.0f;
  float width = 0.0f;
  // Wrap row: uniform children flowing under Constraints::maxWidth.
  float childW = 0.0f;
  float childH = 0.0f;
  // Baseline box: explicit size plus a declared baseline.
  float height = 0.0f;
  float baseline = 0.0f;
  // Unsupported: carries the bogus kind for diagnostics.
  int bogusKind = 0;
};

Node makeGrid(int columns, float cellW, float cellH, int childCount) noexcept;
Node makeAspectBox(float ratio, float width) noexcept;
Node makeWrapRow(float childW, float childH, int childCount) noexcept;
Node makeBaselineBox(float width, float height, float baseline) noexcept;
Node makeUnsupported(int bogusKind) noexcept;

// Measures one responsive primitive under the given constraints.
// Never throws: unsupported kinds yield kUnsupportedProperty and malformed
// inputs yield kInvalidMetrics instead of a silent fallback size.
LayoutResult measureResponsive(Node n, Constraints c) noexcept;

}  // namespace layout
