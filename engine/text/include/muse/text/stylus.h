#pragma once

// FR-TEXT-004 / AC-TEXT-007: C++ stylus sample batch pipeline.
// Pure C++ entry point over the input-subsystem sample batch (historical +
// anchor + predicted). No JNI/JVM types appear here or on the per-sample
// path; platform binding stays a thin layer above this function.

#include <vector>

namespace muse::text {

// Mirrors input::AxisState fields (see engine/input stylus_history.h /
// event_model.h): position, pressure, tilt, orientation plus eraser/hover
// flags. Plain value struct, copied per sample by ExpandSamples.
struct StylusSample {
  float x = 0.0f;
  float y = 0.0f;
  float pressure = 0.0f;
  float tiltX = 0.0f;
  float tiltY = 0.0f;
  float orientation = 0.0f;
  bool eraser = false;
  bool hover = false;
};

// Expands one synthetic batch (1 anchor + N history + M predicted) into a
// single ordered vector: history first, then anchor, then predicted. Every
// axis and flag is copied intact per sample; no filtering, clamping, or
// JNI/JVM hop. Never throws (bad_alloc aside).
std::vector<StylusSample> ExpandSamples(
    const StylusSample& anchor, const std::vector<StylusSample>& history,
    const std::vector<StylusSample>& predicted);

}  // namespace muse::text
