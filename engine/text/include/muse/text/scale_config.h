#pragma once

// FR-TEXT-012 / AC-TEXT-031: in-place config handling — scale change with no
// recreation, no scroll/field state loss. Pure state-threading model of the
// zero-restart path: the same UiState survives the scale update; only metrics
// are recomputed (via T5 RemeasureDirty). Manifest configChanges wiring is
// platform-side and out of scope.

#include <cstddef>
#include <cstdint>
#include <string>

namespace muse::text {

// Scroll/field state retained across a font-scale change.
struct UiState {
  uint64_t scrollPos = 0u;
  std::string fieldText;
  size_t cursor = 0u;
};

// Applies a font-scale change in place: resolves (clamps) newFontScale via
// the T2 NonlinearScale curve, recomputes metrics through T5 RemeasureDirty,
// and returns the state with scrollPos/fieldText/cursor preserved exactly.
UiState ApplyScaleConfig(UiState state, float newFontScale, float density);

}  // namespace muse::text
