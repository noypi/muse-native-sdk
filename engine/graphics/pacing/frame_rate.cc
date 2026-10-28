// Per-surface intended frame-rate selection (FR-GRAPHICS-007).
// Pure logic, no Surface.setFrameRate calls.
#include "engine/graphics/pacing/frame_rate.h"

namespace pacing {

FrameRateIntent selectIntendedFrameRate(ContentClass content,
                                        DisplayModeRange range,
                                        bool ltpoCapable) {
  FrameRateIntent out;
  if (range.minHz <= 0 || range.maxHz < range.minHz) {
    out.fps = 0;
    out.lowPowerIdle = false;
    out.status = Status::kInvalidArg;
    return out;
  }
  out.status = Status::kOk;
  if (content == ContentClass::kIdle && ltpoCapable) {
    out.fps = range.minHz;
    out.lowPowerIdle = true;
    return out;
  }
  out.fps = range.maxHz;
  out.lowPowerIdle = false;
  return out;
}

}  // namespace pacing
