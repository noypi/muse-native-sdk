#include "pviews/canvas.h"

#include <cmath>
#include <exception>

namespace {

// Depth cap: save() saturates here instead of overflowing int on adversarial
// repetition. Well above any balanced nesting the tests exercise.
constexpr int kMaxDepth = 1024;

bool isEmptyRect(const RectF& r) noexcept {
  return r.right <= r.left || r.bottom <= r.top;
}

bool isFiniteRect(const RectF& r) noexcept {
  return std::isfinite(r.left) && std::isfinite(r.top) &&
         std::isfinite(r.right) && std::isfinite(r.bottom);
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
  // Non-finite rect is an ok no-op (never recorded): draw has no rect-denial
  // code, and the empty-rect precedent is ok-no-op. NaN must not propagate
  // toward the Vulkan DAG consumer.
  if (!isFiniteRect(r)) {
    return accepted();
  }
  // Empty rect is an ok no-op (only clipRect denies CLIP_EMPTY).
  if (ops_.size() >= kMaxOps) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  try {
    Op op;
    op.kind = OpKind::kDrawRect;
    op.rect = r;
    op.paint = p;
    ops_.push_back(op);
  } catch (const std::bad_alloc&) {
    return denied(CanvasErrorCode::RECORD_FULL);
  } catch (...) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
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
  try {
    Op op;
    op.kind = OpKind::kDrawPath;
    op.paint = p;
    op.path = path;
    ops_.push_back(op);
  } catch (const std::bad_alloc&) {
    return denied(CanvasErrorCode::RECORD_FULL);
  } catch (...) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  return accepted();
}

int Canvas::save() noexcept {
  // Record-then-bump: the op is queued, never flushed; depth tokens are
  // strictly increasing so nested save/save/restore/restore balances.
  // save has no RecordStatus channel (returns int), so a full queue or an
  // allocation failure still bumps depth to preserve the token contract;
  // depth saturates at kMaxDepth instead of overflowing int.
  if (depth_ < kMaxDepth) {
    if (ops_.size() < kMaxOps) {
      try {
        Op op;
        op.kind = OpKind::kSave;
        ops_.push_back(op);
      } catch (...) {
        // Allocation failure: depth bump below still preserves the token
        // contract; the overflow proof belongs to the T4 budget/graph path.
      }
    }
    ++depth_;
  }
  return depth_;
}

RecordStatus Canvas::restore() noexcept {
  if (depth_ <= 0) {
    return denied(CanvasErrorCode::NONE);
  }
  // Bookkeeping pops even if the queue is full, keeping the stack balanced;
  // overflow proof is T4-owned.
  if (ops_.size() < kMaxOps) {
    try {
      Op op;
      op.kind = OpKind::kRestore;
      ops_.push_back(op);
    } catch (...) {
      // Allocation failure: depth decrement below still keeps the stack
      // balanced.
    }
  }
  --depth_;
  return accepted();
}

RecordStatus Canvas::clipRect(RectF r) noexcept {
  // Non-finite components are denied as CLIP_EMPTY: NaN comparisons defeat
  // isEmptyRect, so finiteness is checked first at the boundary.
  if (!isFiniteRect(r) || isEmptyRect(r)) {
    return denied(CanvasErrorCode::CLIP_EMPTY);
  }
  if (ops_.size() >= kMaxOps) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  try {
    Op op;
    op.kind = OpKind::kClipRect;
    op.rect = r;
    ops_.push_back(op);
  } catch (const std::bad_alloc&) {
    return denied(CanvasErrorCode::RECORD_FULL);
  } catch (...) {
    return denied(CanvasErrorCode::RECORD_FULL);
  }
  return accepted();
}
