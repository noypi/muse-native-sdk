// T2 — FR-TEXT-012 / AC-TEXT-029: nonlinear font-scale curve 85%–200%.
// Hermetic harness: plain C++17, no test framework, no device, no downloads.
// Cases drive only the public font_scale interface with inline dummy inputs.
// Shape mirrors engine/text/tests/jni_free_path_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cmath>
#include <cstdio>
#include <limits>
#include <type_traits>

#include "muse/text/font_scale.h"

// Plan Interface line (verbatim):
// float NonlinearScale(float fontScale); // input 0.85..2.0 → effective
// multiplier; float ScaledSizePx(float sizePx, float fontScale,
// float density);

namespace {

// ASSUMPTION: exact closed-form pins. The plan pins "spot values" but gives
// no numbers; these encode a sub-linear-above-1.0 / near-linear-below-1.0
// A14-equivalent curve. Coder implements the closed form to match within
// kTol; the structural properties (identity, monotonicity, enclosure) hold
// for any conforming curve.
constexpr float kPinned085 = 0.85f;
constexpr float kPinned100 = 1.0f;
constexpr float kPinned130 = 1.18f;
constexpr float kPinned150 = 1.29f;
constexpr float kPinned200 = 1.50f;
constexpr float kTol = 0.06f;

bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

}  // namespace

static bool check_identity_at_100_percent() {
  if (!near(muse::text::NonlinearScale(1.0f), 1.0f, 1e-6f)) return false;
  return true;
}

static bool check_spot_value_085() {
  if (!near(muse::text::NonlinearScale(0.85f), kPinned085, kTol)) return false;
  return true;
}

static bool check_spot_value_130() {
  if (!near(muse::text::NonlinearScale(1.3f), kPinned130, kTol)) return false;
  return true;
}

static bool check_spot_value_150() {
  if (!near(muse::text::NonlinearScale(1.5f), kPinned150, kTol)) return false;
  return true;
}

static bool check_spot_value_200() {
  if (!near(muse::text::NonlinearScale(2.0f), kPinned200, kTol)) return false;
  return true;
}

static bool check_monotonic_across_range() {
  float prev = muse::text::NonlinearScale(0.85f);
  for (int i = 1; i <= 23; ++i) {
    const float s = 0.85f + 0.05f * static_cast<float>(i);  // .. 2.0
    const float cur = muse::text::NonlinearScale(s);
    if (!(cur >= prev)) return false;
    prev = cur;
  }
  return true;
}

static bool check_sublinear_compression_above_100() {
  // Effective multiplier grows slower than the raw scale above 1.0.
  const float e130 = muse::text::NonlinearScale(1.3f);
  const float e200 = muse::text::NonlinearScale(2.0f);
  if (!(e130 < 1.3f)) return false;
  if (!(e200 < 2.0f)) return false;
  if (!(e200 > e130)) return false;
  return true;
}

static bool check_scaled_size_px_applies_curve_times_density() {
  const float sizePx = 16.0f;
  const float density = 2.0f;
  const float got = muse::text::ScaledSizePx(sizePx, 1.3f, density);
  const float want =
      sizePx * muse::text::NonlinearScale(1.3f) * density;
  if (!near(got, want, 1e-4f)) return false;
  return true;
}

static bool check_scaled_size_px_identity_at_100() {
  if (!near(muse::text::ScaledSizePx(16.0f, 1.0f, 2.0f), 32.0f, 1e-4f))
    return false;
  return true;
}

static bool check_scaled_size_px_density_scales_linearly() {
  const float at1 = muse::text::ScaledSizePx(10.0f, 1.5f, 1.0f);
  const float at3 = muse::text::ScaledSizePx(10.0f, 1.5f, 3.0f);
  if (!(at1 > 0.0f)) return false;
  if (!near(at3, 3.0f * at1, 1e-4f)) return false;
  return true;
}

static bool check_line_box_encloses_ascender_descender_at_200() {
  // ASSUMPTION: ascender/descender ratios and line-box factor. Derived from
  // the scaled size so they cannot clip: ascender 0.80×scaled,
  // descender 0.25×scaled, line box 1.20×scaled encloses their sum.
  const float scaled = muse::text::ScaledSizePx(16.0f, 2.0f, 1.0f);
  if (!(scaled > 0.0f)) return false;
  const float ascender = 0.80f * scaled;
  const float descender = 0.25f * scaled;
  const float lineBox = 1.20f * scaled;
  if (!(lineBox >= ascender + descender)) return false;
  // And the 200% box is strictly larger than the 100% box (no collapse).
  const float base = muse::text::ScaledSizePx(16.0f, 1.0f, 1.0f);
  if (!(scaled > base)) return false;
  return true;
}

static bool check_out_of_range_input_stays_finite_and_bounded() {
  // ASSUMPTION: inputs outside 0.85..2.0 clamp to the endpoint multipliers
  // (no NaN/inf, no extrapolation blowup).
  const float lo = muse::text::NonlinearScale(0.5f);
  const float hi = muse::text::NonlinearScale(3.0f);
  if (!(std::isfinite(lo) && std::isfinite(hi))) return false;
  if (!near(lo, muse::text::NonlinearScale(0.85f), 1e-6f)) return false;
  if (!near(hi, muse::text::NonlinearScale(2.0f), 1e-6f)) return false;
  return true;
}

static bool check_nonfinite_input_clamps_to_endpoints() {
  // Interface: NaN/inf clamp to endpoint multipliers, never NaN/inf.
  const float nanLo = muse::text::NonlinearScale(
      std::numeric_limits<float>::quiet_NaN());
  const float posInf = muse::text::NonlinearScale(
      std::numeric_limits<float>::infinity());
  const float negInf = muse::text::NonlinearScale(
      -std::numeric_limits<float>::infinity());
  if (!(std::isfinite(nanLo) && std::isfinite(posInf) &&
        std::isfinite(negInf)))
    return false;
  if (!near(nanLo, muse::text::NonlinearScale(0.85f), 1e-6f)) return false;
  if (!near(posInf, muse::text::NonlinearScale(2.0f), 1e-6f)) return false;
  if (!near(negInf, muse::text::NonlinearScale(0.85f), 1e-6f)) return false;
  return true;
}

static bool check_zero_size_px_scales_to_zero() {
  if (!near(muse::text::ScaledSizePx(0.0f, 1.5f, 2.0f), 0.0f, 1e-6f))
    return false;
  return true;
}

static bool check_signatures_match_plan() {
  static_assert(std::is_same_v<decltype(&muse::text::NonlinearScale),
                               float (*)(float)>);
  static_assert(std::is_same_v<decltype(&muse::text::ScaledSizePx),
                               float (*)(float, float, float)>);
  return true;
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_identity_at_100_percent);
  RUN_CASE(check_spot_value_085);
  RUN_CASE(check_spot_value_130);
  RUN_CASE(check_spot_value_150);
  RUN_CASE(check_spot_value_200);
  RUN_CASE(check_monotonic_across_range);
  RUN_CASE(check_sublinear_compression_above_100);
  RUN_CASE(check_scaled_size_px_applies_curve_times_density);
  RUN_CASE(check_scaled_size_px_identity_at_100);
  RUN_CASE(check_scaled_size_px_density_scales_linearly);
  RUN_CASE(check_line_box_encloses_ascender_descender_at_200);
  RUN_CASE(check_out_of_range_input_stays_finite_and_bounded);
  RUN_CASE(check_nonfinite_input_clamps_to_endpoints);
  RUN_CASE(check_zero_size_px_scales_to_zero);
  RUN_CASE(check_signatures_match_plan);
  if (failures > 0) {
    std::printf("nonlinear_scale_test: %d failing\n", failures);
    return 1;
  }
  std::printf("nonlinear_scale_test: all passing\n");
  return 0;
}
