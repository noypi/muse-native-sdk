// Per-surface intended frame-rate selection (FR-GRAPHICS-007).
// Pure logic, no Surface.setFrameRate calls.
#ifndef ENGINE_GRAPHICS_PACING_FRAME_RATE_H_
#define ENGINE_GRAPHICS_PACING_FRAME_RATE_H_

#include "engine/graphics/pacing/pacing_status.h"

namespace pacing {

enum class ContentClass { kIdle, kScroll, kAnimation, kVideo };

struct DisplayModeRange {
  int minHz;
  int maxHz;
};

struct FrameRateIntent {
  int fps = 0;
  bool lowPowerIdle = false;
  Status status = Status::kOk;
};

// Selects the intended frame rate for one surface given its content class.
// Idle on an LTPO-capable panel selects the low end with lowPowerIdle=true;
// scroll/animation/video and fixed (non-LTPO) panels select the high end.
// Never errors on a valid range; an invalid range (minHz<=0, maxHz<minHz)
// returns Status::kInvalidArg with fps=0. Status-enum only, no exceptions.
FrameRateIntent selectIntendedFrameRate(ContentClass content,
                                        DisplayModeRange range,
                                        bool ltpoCapable);

}  // namespace pacing

#endif  // ENGINE_GRAPHICS_PACING_FRAME_RATE_H_
