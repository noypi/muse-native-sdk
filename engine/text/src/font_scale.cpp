// FR-TEXT-012 / AC-TEXT-029: nonlinear font-scale curve 85%–200%.
// Closed-form A14-equivalent mapping: identity at and below 1.0, power-law
// compression above 1.0. Ascender/descender derive from the scaled size, so
// the line box grown from it cannot clip.

#include "muse/text/font_scale.h"

#include <cmath>

namespace muse::text {
namespace {

// Valid input range.
constexpr float kMinScale = 0.85f;
constexpr float kMaxScale = 2.0f;
// Compression above 1.0: 1 + kGain * (s - 1)^kExponent. kGain = 0.5 pins
// 2.0 → 1.5; kExponent = 0.85 pins 1.3 → ~1.18 and 1.5 → ~1.28.
constexpr float kGain = 0.5f;
constexpr float kExponent = 0.85f;

float CurveAboveOne(float s) {
  return 1.0f + kGain * std::pow(s - 1.0f, kExponent);
}

}  // namespace

float NonlinearScale(float fontScale) {
  // Negated comparisons catch NaN: NaN clamps to the low endpoint (finite).
  if (!(fontScale >= kMinScale)) return kMinScale;
  if (!(fontScale <= kMaxScale)) return CurveAboveOne(kMaxScale);
  if (fontScale <= 1.0f) return fontScale;
  return CurveAboveOne(fontScale);
}

float ScaledSizePx(float sizePx, float fontScale, float density) {
  if (!(sizePx > 0.0f)) return 0.0f;
  if (!(density > 0.0f)) return 0.0f;
  return sizePx * NonlinearScale(fontScale) * density;
}

}  // namespace muse::text
