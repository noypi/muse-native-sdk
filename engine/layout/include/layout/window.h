#pragma once

#include <cstddef>
#include <cstdint>

namespace layout {

using WindowId = std::uint32_t;

inline constexpr WindowId kInvalidWindowId =
    static_cast<WindowId>(0xFFFFFFFFu);

struct PxSize {
  float width = 0.0f;
  float height = 0.0f;
};

struct PxPoint {
  float x = 0.0f;
  float y = 0.0f;
};

struct WindowBounds {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

// Opaque drag-session handle (T4): the session id plays no role in
// hit-testing; it is carried so call sites can correlate drag queries.
struct DragSession {
  std::uint32_t id = 0;
};

// The drop target is the owning window id (T4 interface: -> WindowId).
using DragTarget = WindowId;

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
// inspector header reuses this enumerator. kUnknownDisplay is the T3
// per-display error: unknown/invalid display ids on the metrics path throw
// it (distinct from kUnknownWindow so callers can tell a bad display id on
// applyDisplayMetrics apart from a bad window id on applyWindowBounds).
enum class LayoutError : std::uint8_t {
  kUnknownWindow,
  kDisabledInRelease,
  kUnknownDisplay,
  kNoTarget
};

struct WindowResizeResult {
  SolveStats stats;
  ScrollAnchor preservedScroll;
  FocusId preservedFocus;
  SelectionRange preservedSelection;
  bool recreated = false;
};

// Per-display metrics (FR-LAYOUT-013, AC-LAYOUT-037). density is the
// display's scale factor; the inset fields are the per-edge system insets
// in dp applied independently per window/display.
struct DisplayMetrics {
  float density = 0.0f;
  float insetLeft = 0.0f;
  float insetTop = 0.0f;
  float insetRight = 0.0f;
  float insetBottom = 0.0f;
};

// Fixture entry point (plan names no constructor): returns a valid id for a
// fresh window with a focused field, a scroll anchor, and a selection so
// preservation is observable. Never throws for valid sizes.
WindowId createWindow(PxSize size);

// Window-existence oracle shared across translation units (solver.cpp owns
// the window store): true when `id` was issued and still stored, false for
// kInvalidWindowId and never-issued ids. Used by the metrics path to reject
// ghost ids; never throws.
bool windowExists(WindowId id) noexcept;

// Test-visible state setters through the public path (preservation contract):
// set non-default focus/scroll/selection on a window so a later
// applyWindowBounds resize must echo the exact values back. Unknown ids
// (including kInvalidWindowId) throw `LayoutError::kUnknownWindow`.
void setWindowFocus(WindowId id, FocusId focus);
void setWindowScroll(WindowId id, ScrollAnchor scroll);
void setWindowSelection(WindowId id, SelectionRange selection);

// Freeform resize entry point (FR-LAYOUT-013, AC-LAYOUT-036). Re-solves the
// dirty subtree only (NFR-LAYOUT-005 pattern, consumed read-only) on each
// continuous-resize step; never recreates (`recreated` is always false,
// including zero-size bounds which are accepted in place). The preserved
// scroll/focus/selection in the result equal their pre-resize values.
// Unknown ids (including kInvalidWindowId) throw
// `LayoutError::kUnknownWindow`.
WindowResizeResult applyWindowBounds(WindowId id, PxSize size,
                                     ResizeOpts opts);

// Per-display density/insets entry point (FR-LAYOUT-013, AC-LAYOUT-037).
// Stores the metrics for the window's display; re-applying one window's
// metrics never alters another window's stored metrics. Unknown ids
// (including kInvalidWindowId) throw `LayoutError::kUnknownDisplay`
// (distinct from kUnknownWindow so a bad display id on this path is told
// apart from a bad window id on applyWindowBounds).
void applyDisplayMetrics(WindowId id, DisplayMetrics m);

// Observation helper for the metrics path: echoes the last metrics applied
// to the window via applyDisplayMetrics. Ids with no stored metrics
// (never applied, or unknown) throw `LayoutError::kUnknownDisplay`.
DisplayMetrics displayMetricsOf(WindowId id);

// Fixture placement entry point (T4, FR-LAYOUT-013 AC-LAYOUT-038): creates a
// window whose committed bounds equal `b` and returns its id. Never throws
// for valid bounds.
WindowId createWindowAt(WindowBounds b);

// Cross-window drop-target resolution mid-resize (T4, FR-LAYOUT-013
// AC-LAYOUT-038). Hit-tests `p` against in-flight bounds: the window under
// resize (identified by committed origin matching `resizing`'s origin) is
// tested against `resizing`, all other windows against their committed
// bounds; the latest matching window wins on overlap. Throws
// `LayoutError::kNoTarget` when the point is outside all windows (no
// nearest-window fallback). `s` is an opaque session handle and plays no
// role in hit-testing. Note: stacked same-origin windows are ambiguous —
// the latest origin-matching window wins.
DragTarget resolveDropTarget(DragSession s, PxPoint p,
                             WindowBounds resizing);

// Scripted resize sweep plus display move (T5, FR-LAYOUT-013 AC-LAYOUT-039).
// Builds its own window from the spec (no caller-side setup): drives the
// spec widths phone->desktop via applyWindowBounds, and when
// moveDisplayMidSweep is true delivers toDisplay in place mid-script via
// applyDisplayMetrics (FR-LIFECYCLE-003 consumed read-only: no lifecycle
// change, no recreation). No error surface: asserts internally and never
// throws; degenerate specs (widthCount == 0, or clamped to 8 entries)
// report stable flags.
struct SweepSpec {
  float widths[8] = {};
  std::size_t widthCount = 0;  // Clamped to 8 entries.
  std::uint32_t focusField = 0;
  std::uint32_t scrollNode = 0;
  DisplayMetrics fromDisplay;
  DisplayMetrics toDisplay;
  bool moveDisplayMidSweep = false;
};

struct SweepReport {
  bool focusKept = false;
  bool scrollKept = false;
  bool densityRelayoutClean = false;
};

SweepReport runResizeSweep(SweepSpec spec);

}  // namespace layout
