#pragma once

// Picture record/replay for Canvas conformance (FR-PVIEWS-014 / AC-PVIEWS-023).
// Scale-parameterized replay of the T2 op vocabulary (drawRect/drawPath).
// Host-side CPU reference only (plan A4); no device lab, no golden files.
// Errors are return values, never thrown.
//
// Consumes canvas_record.h (RenderGraph/FrameBudget) from T4; the T5
// replay-probe surface (replayWidth/replayHeight/replayPixelAt plus the
// setReplayRaster population hook used by replayPicture) lives on
// RenderGraph in canvas_record.h per the T5 Files line.

#include <cstddef>

#include "pviews/canvas.h"
#include "pviews/canvas_record.h"

enum class ReplayErrorCode {
  NONE = 0,
  EMPTY_PICTURE = 1,
  BAD_SCALE = 2,
  TOLERANCE_EXCEEDED = 3,
};

struct ReplayResult {
  bool ok = false;
  float maxPixelDelta = 0.0f;
  ReplayErrorCode error = ReplayErrorCode::NONE;
};

// TEST-GOLDEN tolerance (TEST-GOLDEN xref). maxPixelDelta unit: absolute
// 8-bit channel distance per pixel.
inline constexpr float kTestGoldenTolerance = 1.0f;

class Picture {
 public:
  Picture() = default;

  RecordStatus drawRect(RectF r, const Paint& p) noexcept;
  RecordStatus drawPath(const Path& path, const Paint& p) noexcept;
  bool empty() const noexcept { return ops_ == 0; }

  // Replay observation: recorded op count. Read-only; consumed by
  // replayPicture to render the CPU reference raster.
  std::size_t recordedOpCount() const noexcept { return ops_; }
  bool recordedOpIsPath(std::size_t i) const noexcept {
    return i < ops_ && is_path_[i];
  }

 private:
  static constexpr std::size_t kMaxOps = 4096;
  std::size_t ops_ = 0;
  // True per op when the op was a drawPath (false for drawRect). Bounded by
  // kMaxOps; only first kMaxOps entries are meaningful.
  bool is_path_[kMaxOps] = {};
};

ReplayResult replayPicture(const Picture& pic, float scale,
                           RenderGraph& graph) noexcept;
