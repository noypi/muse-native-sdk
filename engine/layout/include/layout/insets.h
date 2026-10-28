#pragma once

#include <array>
#include <cstdint>

namespace layout {

enum class InsetType : std::uint8_t {
  SystemBars = 0,
  DisplayCutout = 1,
  Ime = 2,
  TappableElement = 3,
  SystemGestures = 4,
  MandatorySystemGestures = 5,
  CaptionBar = 6,
  NavigationBars = 7
};

struct EdgeInsets {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

// Typed bus carrying all `WindowInsets.Type` values per edge, published
// through the state-reactivity inset channel and fed by the ALooper path
// (both consumed, not implemented here). Each type is independently
// queryable per edge with no cross-type leakage; every `setInset`
// publishes the latest value (last-write-wins) and bumps only that type's
// revision by exactly one. Unknown types read zero. Never throws.
class InsetBus {
 public:
  InsetBus() = default;

  void setInset(InsetType t, const EdgeInsets& e) noexcept;
  EdgeInsets queryInset(InsetType t) const noexcept;
  std::uint64_t revision(InsetType t) const noexcept;

  // T7: per-vsync IME frames; stale frameId (<= current) ignored.
  // Writes the Ime bottom edge only.
  void onImeFrame(float imeBottom, std::uint64_t frameId) noexcept;
  std::uint64_t imeFrameId() const noexcept;

  // T8: cached safe-area snapshot (per-edge UNION of SystemBars +
  // DisplayCutout + NavigationBars). Recomputed only when a contributor's
  // revision changes; cache hits touch only the cached struct. Never throws,
  // never allocates.
  EdgeInsets safeArea() const noexcept;

 private:
  static constexpr std::size_t kTypeCount = 8;
  std::array<EdgeInsets, kTypeCount> insets_{};
  std::array<std::uint64_t, kTypeCount> revisions_{};
  std::uint64_t ime_frame_id_ = 0;
  // T8 snapshot cache: last published composite + the contributor revisions
  // it was built from. Mutable so the const query path stays side-effect
  // free with respect to `revision()`.
  mutable EdgeInsets safe_area_{};
  mutable std::uint64_t safe_rev_bars_ = 0;
  mutable std::uint64_t safe_rev_cutout_ = 0;
  mutable std::uint64_t safe_rev_nav_ = 0;
  mutable bool safe_dirty_ = true;
};

}  // namespace layout
