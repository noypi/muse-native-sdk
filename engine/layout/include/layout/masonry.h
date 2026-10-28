#pragma once

#include <functional>
#include <vector>

#include "layout/delegate.h"

namespace layout {

// Masonry delegate: staggered multi-column solver with height estimation
// (T5, FR-LAYOUT-006 / AC-LAYOUT-013 masonry half). Pure C++ geometry — no
// JS or bridge calls on this path (NFR-LAYOUT-001 constraint).
//
// Subtree convention (positional, matching the test contract in
// unit-tests/T5.md): the delegate is registered at a container root whose
// children, in insertion (id) order, are the masonry items. Item heights
// arrive via setEstimatedHeight/setMeasuredHeight (estimates until measured
// values arrive); items never estimated fall back to the natural flex size
// the Solver supplies at frame time. Precondition: items without an
// estimate or measurement share the calling node's natural height as their
// fallback.
//
//   columnW = (containerW - (columns - 1) * columnGap) / columns
//   x(item) = column * (columnW + columnGap)
//   y(item) stacks greedily on the shortest column, itemGap between items.
//
// Column assignment is stable: greedy shortest-column placement binds each
// item to its column on first sight, and estimate→measure correction only
// re-stacks y positions within the affected column — items never migrate
// across columns, so unaffected columns are byte-identical after a
// correction (the T5 `Interface:` guarantee).
//
// Solver integration mirrors the T4 sliver path: frames are installed via
// frameFor() by the Solver write-back hook before measure/arrange dispatch,
// so the bounds forwarded to arrange() equal boxOf(id) (the T3
// arrange-bounds invariant). measure/arrange themselves are no-ops.
struct MasonryConfig {
  int columns = 1;
  float columnGap = 0.0f;
  float itemGap = 0.0f;
};

class MasonryDelegate : public LayoutDelegate {
 public:
  explicit MasonryDelegate(MasonryConfig c);

  // Records an estimated (virtualized) height for `item`. Invalid ids are
  // ignored. Negative/NaN heights sanitize to 0 (no clamping rule is pinned
  // by the contract; this only guards the stack math, never throws).
  void setEstimatedHeight(NodeId item, float h);

  // Records a measured height for `item`, replacing the estimate. The item
  // keeps its bound column; only that column re-stacks. Never throws.
  void setMeasuredHeight(NodeId item, float h);

  // Pool seam (T5 Detail): installs the caller-supplied poster used to batch
  // estimation work onto the core worker pool. The delegate never touches a
  // pool directly. When installed, height updates additionally post an
  // idempotent revalidation task through it; frame computation itself stays
  // synchronous so columnOf/boxOf read current values. Never throws.
  // `nullptr` (or an empty function) uninstalls the hook.
  void setTaskPoster(std::function<void(std::function<void()>)> poster);

  // LayoutDelegate surface: measure/arrange are intentionally no-ops. The
  // Solver installs exact frames via frameFor() before dispatching, so the
  // bounds forwarded to arrange() already equal boxOf(id) (T3 invariant).
  void measure(NodeId id, float availW, float availH) override;
  void arrange(NodeId id, Box bounds) override;
  bool handles(NodeId id) const override;

  // Bound column of `item`, or -1 for unknown items (including
  // kInvalidNodeId). Never throws.
  int columnOf(NodeId item) const;

  // Computes the exact frame for one node of the masonry subtree. `rootBox`
  // is the solved container frame of the delegate root, `childIndex` is the
  // node's position among the root's children in id order (-1 for the root
  // itself), and (naturalW, naturalH) are the node's flex sizes registered
  // before delegation (fallback for items with no estimate/measurement).
  // Returns true and fills *out on success; returns false for
  // kInvalidNodeId or a null out pointer. Never throws. Pure function of
  // (config, height table) plus the solver-supplied placement context.
  bool frameFor(NodeId id, const Box& rootBox, int childIndex, float naturalW,
                float naturalH, Box* out) const;

 private:
  float effectiveHeight(NodeId item, float naturalH) const noexcept;
  float columnWidth(float containerW) const noexcept;
  float stackHeight(int col) const;
  int pickShortestColumn() const;
  void bindNewItem(NodeId item) const;
  void postRefresh() const;

  MasonryConfig config_;
  // Insertion-ordered item list (first-seen order drives greedy binding).
  // Mutable so frameFor() can bind previously unseen ids on the fly while
  // staying const/noexcept like SliverDelegate::frameFor.
  mutable std::vector<NodeId> order_;
  mutable std::vector<float> estimates_;  // parallel to order_
  mutable std::vector<float> measured_;   // parallel to order_
  mutable std::vector<bool> hasEstimate_;
  mutable std::vector<bool> hasMeasured_;
  mutable std::vector<int> columns_;  // parallel to order_; -1 = unbound
  std::function<void(std::function<void()>)> poster_;
};

}  // namespace layout
