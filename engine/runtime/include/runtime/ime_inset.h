#pragma once

namespace runtime {

// IME inset seam (FR-RUNTIME-004 / AC-RUNTIME-010). Hermetic: animated
// inset heights and frame ids are passed as arguments; no clock, no
// platform IME, no I/O. The applied frame is recorded on every apply so
// re-layout lands within one frame of the announcement.
struct LayoutViewport {
  float ime_inset_height = 0.0f;
  int inset_announced_frame = 0;
  int applied_frame = 0;
};

// Record the inset height for frame_id. Negative heights clamp to 0 (hidden
// keyboard is represented by a 0 height). Never throws.
inline void apply_ime_inset(LayoutViewport& v, float inset_height, int frame_id) {
  v.ime_inset_height = inset_height < 0.0f ? 0.0f : inset_height;
  v.applied_frame = frame_id;
}

// Frame id of the last apply_ime_inset call. Callers assert
// inset_applied_frame(v) - v.inset_announced_frame <= 1.
inline int inset_applied_frame(const LayoutViewport& v) { return v.applied_frame; }

}  // namespace runtime
