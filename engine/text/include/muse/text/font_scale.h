#pragma once

// FR-TEXT-012 / AC-TEXT-029: nonlinear font-scale curve 85%–200%.
// Closed-form A14-equivalent mapping applied per run before measurement.
// Sub-linear above 1.0, identity at 1.0, near-linear below. Out-of-range
// inputs clamp to the endpoint multipliers; never NaN/inf from finite
// endpoints, never extrapolates.

namespace muse::text {

// Input 0.85..2.0 → effective multiplier. Identity at 1.0, monotonic,
// sub-linear above 1.0. Inputs outside 0.85..2.0 (including NaN/inf)
// clamp to the endpoint multipliers.
float NonlinearScale(float fontScale);

// Per-run application: sizePx * NonlinearScale(fontScale) * density.
// Non-positive sizePx or density yields 0.
float ScaledSizePx(float sizePx, float fontScale, float density);

}  // namespace muse::text
