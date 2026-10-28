#include "layout/insets.h"

namespace layout {
namespace {

std::size_t toIndex(InsetType t) noexcept {
  return static_cast<std::size_t>(static_cast<std::uint8_t>(t));
}

}  // namespace

void InsetBus::setInset(InsetType t, const EdgeInsets& e) noexcept {
  const std::size_t i = toIndex(t);
  if (i >= kTypeCount) {
    return;
  }
  insets_[i] = e;
  revisions_[i] += 1u;
}

EdgeInsets InsetBus::queryInset(InsetType t) const noexcept {
  const std::size_t i = toIndex(t);
  if (i >= kTypeCount) {
    return EdgeInsets{};
  }
  return insets_[i];
}

std::uint64_t InsetBus::revision(InsetType t) const noexcept {
  const std::size_t i = toIndex(t);
  if (i >= kTypeCount) {
    return 0u;
  }
  return revisions_[i];
}

// T7: per-vsync IME frames. A frame newer than the current imeFrameId
// atomically publishes the full Ime bottom edge ({0,0,0,b}) and bumps only
// the Ime revision; stale ids (<= current) are fully ignored. Never throws.
void InsetBus::onImeFrame(float imeBottom, std::uint64_t frameId) noexcept {
  if (frameId <= ime_frame_id_) {
    return;
  }
  const std::size_t kIme = toIndex(InsetType::Ime);
  insets_[kIme].left = 0.0f;
  insets_[kIme].top = 0.0f;
  insets_[kIme].right = 0.0f;
  insets_[kIme].bottom = imeBottom;
  revisions_[kIme] += 1u;
  ime_frame_id_ = frameId;
}

std::uint64_t InsetBus::imeFrameId() const noexcept { return ime_frame_id_; }

}  // namespace layout
