#include "pviews/canvas.h"

namespace {

bool isEmptyRect(const RectF& r) noexcept {
  return r.right <= r.left || r.bottom <= r.top;
}

RecordStatus denied(CanvasErrorCode code) noexcept {
  RecordStatus s;
  s.ok = false;
  s.error = code;
  return s;
}

RecordStatus accepted() noexcept {
  RecordStatus s;
  s.ok = true;
  s.error = CanvasErrorCode::NONE;
  return s;
}

}  // namespace

RecordStatus Canvas::drawRect(RectF r, const Paint& p) noexcept {
  if (!p.valid) {
    return denied(CanvasErrorCode::BAD_PAINT);
  }
  // Empty rect is an ok no-op (only clipRect denies CLIP_EMPTY).
  if (ops_.size() >= kMaxOps) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  Op op;
  op.kind = OpKind::kDrawRect;
  op.rect = r;
  op.paint = p;
  ops_.push_back(op);
  return accepted();
}

RecordStatus Canvas::drawPath(const Path& path, const Paint& p) noexcept {
  if (!p.valid) {
    return denied(CanvasErrorCode::BAD_PAINT);
  }
  if (path.empty) {
    return denied(CanvasErrorCode::EMPTY_PATH);
  }
  if (ops_.size() >= kMaxOps) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  Op op;
  op.kind = OpKind::kDrawPath;
  op.paint = p;
  op.path = path;
  ops_.push_back(op);
  return accepted();
}

int Canvas::save() noexcept {
  // Record-then-bump: the op is queued, never flushed; depth tokens are
  // strictly increasing so nested save/save/restore/restore balances.
  // save has no RecordStatus channel (returns int), so a full queue still
  // bumps depth to preserve the token contract; the overflow proof belongs
  // to the T4 budget/graph path.
  if (ops_.size() < kMaxOps) {
    Op op;
    op.kind = OpKind::kSave;
    ops_.push_back(op);
  }
  ++depth_;
  return depth_;
}

RecordStatus Canvas::restore() noexcept {
  if (depth_ <= 0) {
    return denied(CanvasErrorCode::NONE);
  }
  // Bookkeeping pops even if the queue is full, keeping the stack balanced;
  // overflow proof is T4-owned.
  if (ops_.size() < kMaxOps) {
    Op op;
    op.kind = OpKind::kRestore;
    ops_.push_back(op);
  }
  --depth_;
  return accepted();
}

RecordStatus Canvas::clipRect(RectF r) noexcept {
  if (isEmptyRect(r)) {
    return denied(CanvasErrorCode::CLIP_EMPTY);
  }
  if (ops_.size() >= kMaxOps) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  Op op;
  op.kind = OpKind::kClipRect;
  op.rect = r;
  ops_.push_back(op);
  return accepted();
}
