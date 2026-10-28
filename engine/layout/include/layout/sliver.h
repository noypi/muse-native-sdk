#pragma once

#include "layout/delegate.h"

namespace layout {

// Sliver delegate: collapsing toolbar + pinned header geometry (T4,
// FR-LAYOUT-006 / AC-LAYOUT-013 sliver half). Pure C++ geometry — no JS or
// bridge calls on this path (NFR-LAYOUT-001 constraint).
//
// Subtree convention (positional, matching the test contract in
// unit-tests/T4.md): the delegate is registered at a root whose children,
// in insertion (id) order, are [toolbar, content, pinned-header]. The
// Solver supplies the child index and the natural flex sizes; all frame
// math lives here.
//
//   fraction  = clamp(offset / (expandedH - collapsedH), 0, 1)
//   toolbarH  = expandedH + fraction * (collapsedH - expandedH)
//   contentY  = rootY + toolbarH            (content sits under the toolbar)
//   pinnedY   = max(contentY, rootY + offset) (sticks at the viewport top)
struct SliverConfig {
  float collapsedH = 0.0f;
  float expandedH = 0.0f;
  float pinnedH = 0.0f;
};

class SliverDelegate : public LayoutDelegate {
 public:
  explicit SliverDelegate(SliverConfig c);

  // Clamps negatives (and NaN) to 0; values past the collapse range saturate
  // the fraction at 1.0 via collapseFraction(). Never throws.
  void setScrollOffset(float px) noexcept;

  // LayoutDelegate surface: measure/arrange are intentionally no-ops. The
  // Solver installs exact frames via frameFor() before dispatching, so the
  // bounds forwarded to arrange() already equal boxOf(id) (T3 invariant).
  void measure(NodeId id, float availW, float availH) override;
  void arrange(NodeId id, Box bounds) override;
  bool handles(NodeId id) const override;

  // Current collapse fraction in [0, 1]. Never throws.
  float collapseFraction() const noexcept;

  // Computes the exact frame for one node of the sliver subtree. `rootBox`
  // is the solved viewport frame of the delegate root, `childIndex` is the
  // node's position among the root's children in id order (-1 for the root
  // itself), and (naturalW, naturalH) are the node's flex sizes registered
  // before delegation (the content size rides these). Returns true and fills
  // *out on success; returns false for unknown children (index >= 3) or a
  // null out pointer. Never throws. Pure function of (config, offset) plus
  // the solver-supplied placement context.
  bool frameFor(NodeId id, const Box& rootBox, int childIndex, float naturalW,
                float naturalH, Box* out) const noexcept;

 private:
  SliverConfig config_;
  float scrollOffset_ = 0.0f;
};

}  // namespace layout
