#pragma once

// C++ Canvas core API recording into Vulkan command buffers (FR-PVIEWS-007).
// All calls record into the command-buffer/render-graph queue and never
// flush synchronously. Errors are return values, never thrown.

#include <vector>

struct RectF {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

struct Paint {
  bool valid = true;
};

struct Path {
  bool empty = false;
};

enum class CanvasErrorCode {
  NONE = 0,
  BAD_PAINT = 1,
  EMPTY_PATH = 2,
  CLIP_EMPTY = 3,
  RECORD_FULL = 4,
};

struct RecordStatus {
  bool ok = false;
  CanvasErrorCode error = CanvasErrorCode::NONE;
};

class Canvas {
 public:
  // T4 record path observation (FR-PVIEWS-014 / AC-PVIEWS-022). Exposes the
  // recorded op queue read-only so recordCanvas can schedule it into the
  // render graph. Record-only: no flush, no mutation.
  enum class OpKind {
    kDrawRect,
    kDrawPath,
    kSave,
    kRestore,
    kClipRect,
  };

  Canvas() = default;

  RecordStatus drawRect(RectF r, const Paint& p) noexcept;
  RecordStatus drawPath(const Path& path, const Paint& p) noexcept;
  int save() noexcept;
  RecordStatus restore() noexcept;
  RecordStatus clipRect(RectF r) noexcept;

  std::size_t recordedOpCount() const noexcept { return ops_.size(); }
  OpKind recordedOpAt(std::size_t i) const noexcept {
    return ops_[i].kind;
  }

 private:

  struct Op {
    OpKind kind = OpKind::kDrawRect;
    RectF rect;
    Paint paint;
    Path path;
  };

  // Ops queued for the Vulkan DAG render-graph path. Appended by every
  // recording call; nothing here flushes synchronously — scheduling/flush
  // belongs to the T4 recordCanvas/graph path, which consumes this queue.
  std::vector<Op> ops_;
  int depth_ = 0;

  static constexpr std::size_t kMaxOps = 4096;
};
