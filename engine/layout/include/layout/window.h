#pragma once

#include <cstdint>

namespace layout {

using WindowId = std::uint32_t;

inline constexpr WindowId kInvalidWindowId =
    static_cast<WindowId>(0xFFFFFFFFu);

struct PxSize {
  float width = 0.0f;
  float height = 0.0f;
};

struct ResizeOpts {
};

// Comparable, default-constructible state snapshots observed through
// WindowResizeResult. Values are owned by the window record and carried
// across resizes unchanged (in-place preservation, AC-LAYOUT-036).
struct ScrollAnchor {
  std::uint32_t node = 0;
  float offset = 0.0f;
  bool operator==(const ScrollAnchor& o) const noexcept {
    return node == o.node && offset == o.offset;
  }
  bool operator!=(const ScrollAnchor& o) const noexcept {
    return !(*this == o);
  }
};

struct FocusId {
  std::uint32_t field = 0;
  bool operator==(const FocusId& o) const noexcept {
    return field == o.field;
  }
  bool operator!=(const FocusId& o) const noexcept {
    return !(*this == o);
  }
};

struct SelectionRange {
  std::uint32_t start = 0;
  std::uint32_t end = 0;
  bool operator==(const SelectionRange& o) const noexcept {
    return start == o.start && end == o.end;
  }
  bool operator!=(const SelectionRange& o) const noexcept {
    return !(*this == o);
  }
};

struct SolveStats {
  std::uint32_t dirtyNodes = 0;
  std::uint32_t solvedNodes = 0;
};

// Thrown by value for bad ids (same throw convention as solver_test.cpp's
// std::out_of_range cases): `throw LayoutError::kUnknownWindow;` is caught
// as `const LayoutError&`. kDisabledInRelease is declared here so the
// inspector release contract (T6) pins against the same type; the T2/T6
// inspector header reuses this enumerator.
enum class LayoutError : std::uint8_t { kUnknownWindow, kDisabledInRelease };

struct WindowResizeResult {
  SolveStats stats;
  ScrollAnchor preservedScroll;
  FocusId preservedFocus;
  SelectionRange preservedSelection;
  bool recreated = false;
};

// Fixture entry point (plan names no constructor): returns a valid id for a
// fresh window with a focused field, a scroll anchor, and a selection so
// preservation is observable. Never throws for valid sizes.
WindowId createWindow(PxSize size);

// Freeform resize entry point (FR-LAYOUT-013, AC-LAYOUT-036). Re-solves the
// dirty subtree only (NFR-LAYOUT-005 pattern, consumed read-only) on each
// continuous-resize step; never recreates (`recreated` is always false,
// including zero-size bounds which are accepted in place). The preserved
// scroll/focus/selection in the result equal their pre-resize values.
// Unknown ids (including kInvalidWindowId) throw
// `LayoutError::kUnknownWindow`.
WindowResizeResult applyWindowBounds(WindowId id, PxSize size,
                                     ResizeOpts opts);

}  // namespace layout
