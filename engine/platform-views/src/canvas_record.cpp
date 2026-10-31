#include "pviews/canvas_record.h"

namespace {

RecordResult denied(RecordErrorCode code, std::uint32_t frame) noexcept {
  RecordResult r;
  r.ok = false;
  r.scheduledFrame = frame;
  r.error = code;
  return r;
}

GraphOpKind mapKind(Canvas::OpKind kind) noexcept {
  switch (kind) {
    case Canvas::OpKind::kDrawRect:
      return GraphOpKind::kDrawRect;
    case Canvas::OpKind::kDrawPath:
      return GraphOpKind::kDrawPath;
    case Canvas::OpKind::kSave:
      return GraphOpKind::kSave;
    case Canvas::OpKind::kRestore:
      return GraphOpKind::kRestore;
    case Canvas::OpKind::kClipRect:
      return GraphOpKind::kClipRect;
  }
  return GraphOpKind::kDrawRect;
}

}  // namespace

RecordResult recordCanvas(const Canvas& canvas, RenderGraph& graph,
                          FrameBudget budget) noexcept {
  const std::size_t count = canvas.recordedOpCount();
  if (count == 0) {
    return denied(RecordErrorCode::EMPTY_CANVAS, budget.currentFrame);
  }
  // Op-count budget unit: work that does not fit defers to the next frame
  // with nothing appended; never blocks the caller. The deferral saturates
  // at UINT32_MAX instead of wrapping to 0.
  if (count > static_cast<std::size_t>(budget.maxOps)) {
    const std::uint32_t deferred =
        (budget.currentFrame == UINT32_MAX) ? UINT32_MAX
                                            : budget.currentFrame + 1;
    return denied(RecordErrorCode::BUDGET_EXCEEDED, deferred);
  }
  if (graph.rejectNext) {
    graph.rejectNext = false;
    return denied(RecordErrorCode::GRAPH_REJECTED, budget.currentFrame);
  }
  for (std::size_t i = 0; i < count; ++i) {
    GraphOp op;
    op.kind = mapKind(canvas.recordedOpAt(i));
    if (!graph.append(op)) {
      return denied(RecordErrorCode::GRAPH_REJECTED, budget.currentFrame);
    }
  }
  RecordResult r;
  r.ok = true;
  r.scheduledFrame = budget.currentFrame;
  r.error = RecordErrorCode::NONE;
  return r;
}
