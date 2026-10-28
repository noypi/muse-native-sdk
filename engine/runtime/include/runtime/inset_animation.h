#pragma once

#include <cstddef>
#include <deque>
#include <unordered_map>

// Inset animation sync (FR-TEXT-008 / AC-TEXT-016). Hermetic: frame-tagged
// inset samples are passed as arguments; no clock, no platform IME, no I/O.
// Each sample maps 1:1 to its frame id so re-layout lands within one frame
// of the announcement. Extends the `runtime/ime_inset.h` seam concept
// without including or modifying that header. Never throws on bad input
// (allocation failure may still throw std::bad_alloc).
struct InsetFrame {
  float ime_height = 0.0f;
  int frame_id = 0;
};

class InsetSync {
 public:
  InsetSync() = default;

  // Record the inset height for f.frame_id. Negative heights clamp to 0
  // (hidden keyboard is represented by a 0 height). Never throws on bad
  // input.
  void on_inset(InsetFrame f) noexcept {
    try {
      const float height = f.ime_height < 0.0f ? 0.0f : f.ime_height;
      const auto it = heights_.find(f.frame_id);
      if (it == heights_.end()) {
        order_.push_back(f.frame_id);
      }
      heights_[f.frame_id] = height;
      // Cap the window to the latest N frame ids; eviction is safe because
      // height_for_frame falls back to the latest known height.
      while (heights_.size() > kMaxFrames) {
        const int oldest = order_.front();
        order_.pop_front();
        heights_.erase(oldest);
      }
      last_frame_ = f.frame_id;
      last_height_ = height;
      has_sample_ = true;
    } catch (...) {
      // Keep the seam usable; retain the previous state.
    }
  }

  // Exact sampled height for a known frame id; the latest known height for
  // an unknown frame id (future queries); 0 when no sample exists. Never
  // throws.
  float height_for_frame(int frame_id) const noexcept {
    try {
      const auto it = heights_.find(frame_id);
      if (it != heights_.end()) {
        return it->second;
      }
      return has_sample_ ? last_height_ : 0.0f;
    } catch (...) {
      return has_sample_ ? last_height_ : 0.0f;
    }
  }

  // Frame id of the most recent on_inset call (0 when no sample exists).
  // Never throws.
  int last_applied_frame() const noexcept { return last_frame_; }

 private:
  std::unordered_map<int, float> heights_;
  std::deque<int> order_;
  static constexpr std::size_t kMaxFrames = 512;
  int last_frame_ = 0;
  float last_height_ = 0.0f;
  bool has_sample_ = false;
};
