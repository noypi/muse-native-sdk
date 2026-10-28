// Single-owner pacing Status (FR-GRAPHICS-007, T7 BLOCKER 2 fix).
// Pure types, no platform calls.
#ifndef ENGINE_GRAPHICS_PACING_PACING_STATUS_H_
#define ENGINE_GRAPHICS_PACING_PACING_STATUS_H_

namespace pacing {

enum class Status { kOk, kInvalidArg };

}  // namespace pacing

#endif  // ENGINE_GRAPHICS_PACING_PACING_STATUS_H_
