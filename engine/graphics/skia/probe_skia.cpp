// Skia backend capability-probe selection leg (FR-003 / AC-GRAPHICS-039 T1).
// Implements `ProbeSkia` from `probe_skia.h`: selection only, mirroring the
// `BackendProbe` selection shape in `engine/runtime/src/presenter.cpp`. No
// Skia SDK calls, no GPU context, no allocation: every return points `reason`
// at one of the static trigger strings below, so the result is trivially
// copyable and pointer-stable across calls.

#include "probe_skia.h"

namespace {

// Static trigger strings: static storage duration, one object each, so two
// identical probes return the same `reason` pointer (no per-call allocation
// to free, nothing to throw from).
constexpr char kSelectedReason[] =
    "skia selected: skia available and CPU baseline ok";
constexpr char kSkiaUnavailableReason[] = "skia skipped: skia unavailable";
constexpr char kBaselineNotOkReason[] =
    "skia skipped: CPU baseline not satisfied";

}  // namespace

SkiaProbeResult ProbeSkia(const SkiaProbeInput& input) noexcept {
  if (!input.skia_available) {
    return {false, kSkiaUnavailableReason};
  }
  if (!input.cpu_baseline_ok) {
    return {false, kBaselineNotOkReason};
  }
  return {true, kSelectedReason};
}
