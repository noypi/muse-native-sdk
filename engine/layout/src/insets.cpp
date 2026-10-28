#include "layout/insets.h"

#include "layout/window.h"

#include <unordered_map>

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

// T8: per-edge UNION (component-wise MAX) of SystemBars + DisplayCutout +
// NavigationBars. Ime/gestures/tappable/caption excluded. Recomputed only
// when a contributor revision changed; otherwise serves the cached struct.
EdgeInsets InsetBus::safeArea() const noexcept {
  const std::size_t kBars = toIndex(InsetType::SystemBars);
  const std::size_t kCutout = toIndex(InsetType::DisplayCutout);
  const std::size_t kNav = toIndex(InsetType::NavigationBars);
  const std::uint64_t rBars = revisions_[kBars];
  const std::uint64_t rCutout = revisions_[kCutout];
  const std::uint64_t rNav = revisions_[kNav];
  if (!safe_dirty_ && rBars == safe_rev_bars_ && rCutout == safe_rev_cutout_ &&
      rNav == safe_rev_nav_) {
    return safe_area_;
  }
  const EdgeInsets& bars = insets_[kBars];
  const EdgeInsets& cutout = insets_[kCutout];
  const EdgeInsets& nav = insets_[kNav];
  auto pickMax = [](float a, float b, float c) noexcept {
    float m = a > b ? a : b;
    return m > c ? m : c;
  };
  safe_area_.left = pickMax(bars.left, cutout.left, nav.left);
  safe_area_.top = pickMax(bars.top, cutout.top, nav.top);
  safe_area_.right = pickMax(bars.right, cutout.right, nav.right);
  safe_area_.bottom = pickMax(bars.bottom, cutout.bottom, nav.bottom);
  safe_rev_bars_ = rBars;
  safe_rev_cutout_ = rCutout;
  safe_rev_nav_ = rNav;
  safe_dirty_ = false;
  return safe_area_;
}

}  // namespace layout

// ---- T3 per-display metrics (FR-LAYOUT-013, AC-LAYOUT-037) ----
// Per-window applied display metrics, keyed by WindowId so each window on
// its own synthetic display keeps its own density and per-edge insets with
// no cross-contamination: re-applying one window's metrics touches only
// that window's entry. Density is stored, not validated, here — the plan's
// T3 interface exposes only the kUnknownDisplay error channel, so there is
// no invalid-density error to report on this path.
namespace layout {
namespace {

// Window issuance is owned by T1's store in solver.cpp and shared here via
// the layout::windowExists oracle. The cap below is only a fast-reject fast
// path; the oracle is the sole gate: never-issued small ids (e.g. 500 with
// only ids 0,1 created) throw kUnknownDisplay.
constexpr WindowId kNeverIssuedCap = 4096;

std::unordered_map<WindowId, DisplayMetrics>& displayMetricsStore() {
  static std::unordered_map<WindowId, DisplayMetrics> store;
  return store;
}

void requireKnownDisplay(WindowId id) {
  if (id == kInvalidWindowId || id >= kNeverIssuedCap) {
    throw LayoutError::kUnknownDisplay;
  }
  if (!windowExists(id)) {
    throw LayoutError::kUnknownDisplay;
  }
}

}  // namespace

void applyDisplayMetrics(WindowId id, DisplayMetrics m) {
  requireKnownDisplay(id);
  displayMetricsStore()[id] = m;
}

DisplayMetrics displayMetricsOf(WindowId id) {
  requireKnownDisplay(id);
  auto& store = displayMetricsStore();
  const auto it = store.find(id);
  if (it == store.end()) {
    throw LayoutError::kUnknownDisplay;
  }
  return it->second;
}

}  // namespace layout
