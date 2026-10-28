#pragma once

#include <vector>

#include "input/event_model.h"

namespace input {

// Replays coalesced/batched stylus samples in order: one event per
// AxisState in `history` followed by `coalesced` itself. Never throws.
std::vector<NormalizedEvent> ExpandHistoricalSamples(
    const NormalizedEvent& coalesced, const std::vector<AxisState>& history);

// Provisional palm heuristic (Q-INPUT-003): a large touch contact observed
// while a stylus is active. Never throws.
bool IsPalmContact(const NormalizedEvent& ev, bool stylusActive) noexcept;

// Process-local unbuffered-dispatch intent flag. The NDK
// requestUnbufferedDispatch call is a platform concern outside unit scope;
// these functions only record and report the intent. Never throws.
void RequestUnbufferedDispatch(bool enable) noexcept;
bool UnbufferedDispatchEnabled() noexcept;

}  // namespace input
