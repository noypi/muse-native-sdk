// Bounded render-ahead queue depth guard (FR-GRAPHICS-007).
// Pure logic, no present-mode or thread calls.
#include "engine/graphics/pacing/render_ahead.h"

namespace pacing {

RenderAheadVerdict tryQueueFrame(int queuedDepth, int maxDepth,
                                 int framesRequested) {
  RenderAheadVerdict out;
  if (queuedDepth < 0 || maxDepth <= 0 || framesRequested < 0) {
    out.admitted = 0;
    out.rejected = 0;
    out.status = Status::kInvalidArg;
    return out;
  }
  out.status = Status::kOk;
  int headroom = maxDepth - queuedDepth;
  if (headroom < 0) headroom = 0;
  out.admitted = framesRequested < headroom ? framesRequested : headroom;
  out.rejected = framesRequested - out.admitted;
  return out;
}

}  // namespace pacing
