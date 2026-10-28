#pragma once

// FR-TEXT-007 / AC-TEXT-010 + AC-TEXT-011: single home for shared text types.
// Both shaper.h and font_discovery.h include this header; no TU may define
// FontHandle/Status/StatusOr twice.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace muse::text {

// Opaque font reference. Real NDK face resolution is T2 (font_discovery);
// the shaper only needs an identifier it can copy by value.
// Equality-comparable so callers can assert ordering determinism.
struct FontHandle {
  FontHandle() = default;
  explicit FontHandle(std::uint64_t id) : id(id) {}
  std::uint64_t id = 0;
  bool operator==(const FontHandle& other) const { return id == other.id; }
  bool operator!=(const FontHandle& other) const { return id != other.id; }
};

// Plain styling inputs for the shaping call. No JVM-owned objects.
struct TextStyle {
  TextStyle() = default;
  float size_px = 16.0f;
  float letter_spacing_px = 0.0f;
};

enum class Status {
  kOk = 0,
  kInvalidScript = 1,
  kNoFaceAvailable = 2,
  // FR-TEXT-001 / AC-TEXT-002: malformed input for bidi/line-break (T5).
  kInvalidInput = 3,
  kUnsupportedSequence = 4,
  // FR-TEXT-003 / AC-TEXT-005: no supported-locale face covers the script
  // (resolveFallbackForScript fallback miss). No payload, no internals.
  kMissingGlyph = 5,
};

// Minimal StatusOr: ok()/status()/operator* surface the contract needs.
template <typename T>
class StatusOr {
 public:
  StatusOr(T value) : status_(Status::kOk), value_(std::move(value)) {}
  StatusOr(Status status) : status_(status) {}

  bool ok() const { return status_ == Status::kOk; }
  Status status() const { return status_; }
  const T& operator*() const { return value_; }
  T& operator*() { return value_; }
  const T* operator->() const { return &value_; }
  T* operator->() { return &value_; }

 private:
  Status status_;
  T value_{};
};

struct GlyphOffset {
  float x = 0.0f;
  float y = 0.0f;
  bool operator==(const GlyphOffset& other) const {
    return x == other.x && y == other.y;
  }
};

struct ShapedRun {
  std::vector<std::uint32_t> glyph_ids;
  std::vector<float> advances;
  std::vector<GlyphOffset> offsets;
  // One entry per input byte: cluster_map[byte_index] = glyph index that
  // owns the byte. Empty when the input is empty.
  std::vector<std::uint32_t> cluster_map;
};

// Non-owning view over a shaped run for the measure step.
class ShapedRunView {
 public:
  ShapedRunView() = default;
  ShapedRunView(const ShapedRun& run) : run_(&run) {}  // NOLINT(runtime/explicit)

  const ShapedRun* run() const { return run_; }
  std::size_t glyph_count() const {
    return run_ != nullptr ? run_->glyph_ids.size() : 0;
  }

 private:
  const ShapedRun* run_ = nullptr;
};

struct MeasuredLine {
  float width_px = 0.0f;
  std::size_t glyph_count = 0;
};

}  // namespace muse::text
