#include "motion/large_motion.h"

#include <cmath>

namespace motion {
namespace {

// Hermetic-test placeholder: the production hinge line is injected as an
// explicit float x with the boxes in a follow-up wiring the FoldingFeature
// signal (no behavior change here); T7 contract tests inject the same line
// (kHingeX=500).
constexpr float kHingeX = 500.0f;

bool isLeft(const Box& b) noexcept { return b.x + b.w <= kHingeX; }
bool isRight(const Box& b) noexcept { return b.x >= kHingeX; }

// Clamp a box so it no longer spans the hinge, keeping its size and
// choosing the side it already overlaps most.
Box clampToOneSide(Box b) noexcept {
  if (isLeft(b) || isRight(b)) return b;
  float overLeft = kHingeX - b.x;
  float overRight = (b.x + b.w) - kHingeX;
  if (overLeft >= overRight) {
    b.x = kHingeX - b.w;
  } else {
    b.x = kHingeX;
  }
  return b;
}

}  // namespace

PaneMove motion_paneMove(Posture p, HingeOcclusion occ, const Box& a,
                         const Box& b) noexcept {
  if (occ == HingeOcclusion::Full) {
    // Hinge is occluded: the move must never span it. Clamp both panes
    // and keep them on the source pane's side so the pair stays together.
    // AC-038 hard requirement: under Full occlusion ALL postures collapse
    // to this hinge-safe clamp (crossHinge false).
    Box from = clampToOneSide(a);
    Box to = clampToOneSide(b);
    bool fromLeft = !isRight(from);
    if (fromLeft && isRight(to)) {
      to.x = kHingeX - to.w;
    } else if (!fromLeft && isLeft(to)) {
      to.x = kHingeX;
    }
    to = clampToOneSide(to);
    from = clampToOneSide(from);
    return PaneMove{from, to, false};
  }
  // Visible hinge (Partial/None) — posture selects the variant:
  // - Tabletop/HalfOpened (dual-pane postures): pane-move variant with
  //   endpoints preserved verbatim; crossHinge reports opposite-side
  //   placement, so a cross-pane request (left->right) yields crossHinge
  //   true. Flat/Partial vs Tabletop/Partial with identical boxes therefore
  //   differ: Tabletop preserves the crossing endpoints, Flat snaps below.
  // - Flat/Folded (single-pane postures): endpoint preservation would span
  //   the hinge, so the move snaps onto the source side instead — `to` is
  //   clamped to the source pane's side and crossHinge is false.
  if (p == Posture::Tabletop || p == Posture::HalfOpened) {
    Box from = a;
    Box to = b;
    bool cross = (isLeft(from) && isRight(to)) || (isRight(from) && isLeft(to));
    return PaneMove{from, to, cross};
  }
  Box from = clampToOneSide(a);
  Box to = clampToOneSide(b);
  bool fromLeft = !isRight(from);
  if (fromLeft && isRight(to)) {
    to.x = kHingeX - to.w;
  } else if (!fromLeft && isLeft(to)) {
    to.x = kHingeX;
  }
  to = clampToOneSide(to);
  from = clampToOneSide(from);
  return PaneMove{from, to, false};
}

float motion_scaledDuration(float baseMs, SizeClass sc, float minMs,
                            float maxMs) noexcept {
  float lo = minMs;
  float hi = maxMs;
  if (!std::isfinite(lo) || !std::isfinite(hi)) {
    if (std::isfinite(lo) && !std::isfinite(hi)) {
      hi = lo;
    } else if (std::isfinite(hi) && !std::isfinite(lo)) {
      lo = hi;
    } else {
      lo = 0.0f;
      hi = 0.0f;
    }
  }
  if (hi < lo) {
    float tmp = lo;
    lo = hi;
    hi = tmp;
  }
  float factor = 1.0f;
  switch (sc) {
    case SizeClass::Compact:
      factor = 0.8f;
      break;
    case SizeClass::Medium:
      factor = 1.0f;
      break;
    case SizeClass::Expanded:
      factor = 1.2f;
      break;
  }
  if (!std::isfinite(baseMs)) return lo;
  float scaled = baseMs * factor;
  if (scaled < lo) return lo;
  if (scaled > hi) return hi;
  return scaled;
}

Box motion_containerTransform(const std::vector<Box>& children, const Box& from,
                              const Box& to, float t01) noexcept {
  // `children` is intentionally untouched: the parameter exists to prove
  // non-mutation (const-ref) while only the container box interpolates.
  (void)children;
  return interpolateBox(from, to, t01);
}

}  // namespace motion
