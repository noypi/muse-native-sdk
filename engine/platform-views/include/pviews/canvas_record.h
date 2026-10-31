#pragma once

// Canvas work recording into the render graph with frame-budget scheduling
// (FR-PVIEWS-014 / AC-PVIEWS-022). Record-only: scheduling appends ops to
// the graph and never flushes synchronously. Errors are return values,
// never thrown.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pviews/canvas.h"

enum class RecordErrorCode {
  NONE = 0,
  EMPTY_CANVAS = 1,
  BUDGET_EXCEEDED = 2,
  GRAPH_REJECTED = 3,
};

struct RecordResult {
  bool ok = false;
  uint32_t scheduledFrame = 0;
  RecordErrorCode error = RecordErrorCode::NONE;
};

// maxOps: op capacity schedulable in currentFrame. A canvas whose op count
// fits schedules at currentFrame; overflow defers to currentFrame+1 with
// BUDGET_EXCEEDED and appends nothing.
struct FrameBudget {
  uint32_t maxOps = 0;
  uint32_t currentFrame = 0;
};

enum class GraphOpKind {
  kDrawRect,
  kDrawPath,
  kSave,
  kRestore,
  kClipRect,
};

struct GraphOp {
  GraphOpKind kind = GraphOpKind::kDrawRect;
};

class RenderGraph {
 public:
  RenderGraph() = default;

  // Appended ops total.
  std::size_t opCount() const noexcept { return ops_.size(); }

  // Appended op in order.
  const GraphOp& opAt(std::size_t i) const { return ops_.at(i); }

  // Synchronous flushes performed. Scheduling via recordCanvas never
  // flushes, so this stays zero on the record path.
  std::uint32_t flushCount() const noexcept { return flushes_; }

  // Test hook: when true, the next append is denied (consumes the flag).
  bool rejectNext = false;

  // Append one op; false when rejected (rejectNext consumed) or on
  // allocation failure. Never throws.
  bool append(GraphOp op) noexcept {
    if (rejectNext) {
      rejectNext = false;
      return false;
    }
    try {
      ops_.push_back(op);
    } catch (...) {
      return false;
    }
    return true;
  }

  // --- T5 replay-probe surface (ADDITIVE ONLY) ---
  // Last replayPicture raster extents. Zero when no replay stored.
  std::uint32_t replayWidth() const noexcept { return replay_w_; }
  std::uint32_t replayHeight() const noexcept { return replay_h_; }
  // Grayscale channel value in [0,255] of the last replay; 0 when no
  // replay stored or coordinates are out of bounds. Never throws.
  float replayPixelAt(std::uint32_t x, std::uint32_t y) const noexcept {
    if (replay_w_ == 0 || replay_h_ == 0) return 0.0f;
    if (x >= replay_w_ || y >= replay_h_) return 0.0f;
    const std::size_t idx =
        static_cast<std::size_t>(y) * replay_w_ + x;
    if (idx >= replay_.size()) return 0.0f;
    return replay_[idx];
  }
  // Store a replay raster for the conformance probes. Called by
  // replayPicture only; never touches the op list, flush count, or
  // rejectNext flag, so T4 record/budget/reject behavior is unchanged.
  // Never throws; on allocation failure the previous raster is kept.
  void setReplayRaster(std::uint32_t w, std::uint32_t h,
                       const std::vector<float>& pixels) noexcept {
    try {
      std::vector<float> copy = pixels;
      const std::size_t want = static_cast<std::size_t>(w) * h;
      if (copy.size() != want) return;
      replay_ = std::move(copy);
      replay_w_ = w;
      replay_h_ = h;
    } catch (...) {
    }
  }

 private:
  std::vector<GraphOp> ops_;
  std::uint32_t flushes_ = 0;
  // Replay raster (T5 probes only; invisible to recordCanvas).
  std::vector<float> replay_;
  std::uint32_t replay_w_ = 0;
  std::uint32_t replay_h_ = 0;
};

RecordResult recordCanvas(const Canvas& canvas, RenderGraph& graph,
                          FrameBudget budget) noexcept;
