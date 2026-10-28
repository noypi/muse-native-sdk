#pragma once

#include "lists/collection_contract.h"

namespace lists {

// Aggregated outcome of the three AC-LISTS-027 end-to-end scenarios:
// contacts index-bar jump, span reflow, and slot state-switch.
struct ScenarioResult {
  bool jumpOk = false;
  bool reflowOk = false;
  bool stateSwitchOk = false;
};

// Changes the span of the cell keyed by `k` to `newSpan`, re-laying-out
// only that cell (its span and frame) while leaving every other cell's
// key, span, and frame untouched — the observable proxy for "no
// whole-list rebind". Returns true on success.
// Returns false with no mutation when `k` is absent or `newSpan` is out
// of range ([1, layout span count]).
bool ReflowSpan(CollectionLayout& l, ItemKey k, int newSpan);

// End-to-end scenario check: true iff the contacts jump, the span reflow,
// and the state switch all succeeded.
bool VerifyShapeScenario(const ScenarioResult& r);

}  // namespace lists
