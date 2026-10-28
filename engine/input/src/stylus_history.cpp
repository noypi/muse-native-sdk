#include "input/stylus_history.h"

namespace input {
namespace {

// Provisional palm cutoff (Q-INPUT-003, device-lab tuning deferred):
// a touch contact at or above this pressure while a stylus is down counts
// as palm. Keeps the in-test 0.4 finger / 0.5 tip below and the 1.0 palm
// above the line.
constexpr float kPalmPressureThreshold = 0.9f;

bool g_unbuffered_dispatch_enabled = false;

}  // namespace

std::vector<NormalizedEvent> ExpandHistoricalSamples(
    const NormalizedEvent& coalesced, const std::vector<AxisState>& history) {
  std::vector<NormalizedEvent> out;
  out.reserve(history.size() + 1);
  for (const AxisState& axes : history) {
    NormalizedEvent ev = coalesced;
    ev.axes = axes;
    out.push_back(ev);
  }
  out.push_back(coalesced);
  return out;
}

bool IsPalmContact(const NormalizedEvent& ev, bool stylusActive) noexcept {
  if (!stylusActive) return false;
  if (ev.tool == ToolType::Stylus || ev.sourceClass == SourceClass::Stylus) {
    return false;
  }
  if (ev.tool != ToolType::Touch && ev.sourceClass != SourceClass::Touch) {
    return false;
  }
  return ev.axes.pressure >= kPalmPressureThreshold;
}

void RequestUnbufferedDispatch(bool enable) noexcept {
  g_unbuffered_dispatch_enabled = enable;
}

bool UnbufferedDispatchEnabled() noexcept {
  return g_unbuffered_dispatch_enabled;
}

}  // namespace input
