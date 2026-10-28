// Bounded render-ahead queue depth guard (FR-GRAPHICS-007).
// Pure logic, no present-mode or thread calls.
#ifndef ENGINE_GRAPHICS_PACING_RENDER_AHEAD_H_
#define ENGINE_GRAPHICS_PACING_RENDER_AHEAD_H_

namespace pacing {

enum class Status { kOk, kInvalidArg };

struct RenderAheadVerdict {
  int admitted = 0;
  int rejected = 0;
  Status status = Status::kOk;
};

// Admits at most the remaining headroom (maxDepth - queuedDepth, floored at
// zero): admitted = min(framesRequested, max(0, maxDepth - queuedDepth)),
// rejected = framesRequested - admitted. Negative inputs or maxDepth<=0
// return Status::kInvalidArg with zeros. Status-enum only, no exceptions.
RenderAheadVerdict tryQueueFrame(int queuedDepth, int maxDepth,
                                 int framesRequested);

}  // namespace pacing

#endif  // ENGINE_GRAPHICS_PACING_RENDER_AHEAD_H_
