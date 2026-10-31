// T6 (FR-TEXT-012 / AC-TEXT-031): in-place config handling — scale change
// with no recreation, no scroll/field state loss.
// Hermetic harness: plain C++17, no test framework, no device, no downloads.
// Cases drive only the public scale_config interface with inline dummy state.
// Shape mirrors engine/text/tests/scale_remeasure_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>

#include "muse/text/scale_config.h"

// Plan Interface line (verbatim):
// ApplyScaleConfig(state: UiState, newFontScale: float, density: float): UiState — resolves scale via NonlinearScale, recomputes metrics via RemeasureDirty, returns state with scrollPos/fieldText/cursor preserved exactly; error shape: none — out-of-range scale clamps, no recreation.

namespace {

muse::text::UiState sampleState() {
  muse::text::UiState s;
  s.scrollPos = 12345u;
  s.fieldText = "hello field";
  s.cursor = 5u;
  return s;
}

bool sameState(const muse::text::UiState& a,
               const muse::text::UiState& b) {
  return a.scrollPos == b.scrollPos && a.fieldText == b.fieldText &&
         a.cursor == b.cursor;
}

}  // namespace

static bool check_scale_up_preserves_state() {
  const muse::text::UiState before = sampleState();
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(before, 1.5f, 2.0f);
  if (!sameState(before, after)) return false;
  return true;
}

static bool check_scale_down_preserves_state() {
  const muse::text::UiState before = sampleState();
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(before, 0.85f, 2.0f);
  if (!sameState(before, after)) return false;
  return true;
}

static bool check_empty_field_preserved() {
  muse::text::UiState s;
  s.scrollPos = 0u;
  s.fieldText = "";
  s.cursor = 0u;
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(s, 2.0f, 3.0f);
  if (!sameState(s, after)) return false;
  return true;
}

static bool check_cursor_at_end_preserved() {
  muse::text::UiState s;
  s.scrollPos = 900u;
  s.fieldText = "abcdef";
  s.cursor = s.fieldText.size();
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(s, 1.3f, 2.0f);
  if (!sameState(s, after)) return false;
  if (after.cursor != after.fieldText.size()) return false;
  return true;
}

static bool check_density_variation_preserves_state() {
  const muse::text::UiState before = sampleState();
  // Same scale, different density: still no state loss.
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(before, 1.0f, 3.0f);
  if (!sameState(before, after)) return false;
  return true;
}

static bool check_repeated_apply_is_stable() {
  // ASSUMPTION: ApplyScaleConfig is a pure state-threading function, so
  // applying the same scale twice yields identical state (no drift, no
  // reset-to-default on the second pass).
  const muse::text::UiState before = sampleState();
  const muse::text::UiState once =
      muse::text::ApplyScaleConfig(before, 1.5f, 2.0f);
  const muse::text::UiState twice =
      muse::text::ApplyScaleConfig(once, 1.5f, 2.0f);
  if (!sameState(once, twice)) return false;
  if (!sameState(before, twice)) return false;
  return true;
}

static bool check_large_scrollpos_preserved() {
  muse::text::UiState s;
  s.scrollPos = UINT64_MAX;
  s.fieldText = "x";
  s.cursor = 1u;
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(s, 2.0f, 3.0f);
  if (!sameState(s, after)) return false;
  return true;
}

static bool check_out_of_range_scale_preserves_state() {
  // ASSUMPTION: out-of-range newFontScale clamps (same policy as T2
  // NonlinearScale) and still preserves scroll/cursor/text — never a reset.
  const muse::text::UiState before = sampleState();
  const muse::text::UiState after =
      muse::text::ApplyScaleConfig(before, 99.0f, 2.0f);
  if (!sameState(before, after)) return false;
  return true;
}

static bool check_signatures_match_plan() {
  static_assert(std::is_same_v<decltype(&muse::text::ApplyScaleConfig),
                               muse::text::UiState (*)(muse::text::UiState,
                                                       float, float)>);
  muse::text::UiState s;
  s.scrollPos = 1u;
  s.fieldText = "x";
  s.cursor = 0u;
  static_assert(std::is_same_v<decltype(s.scrollPos), uint64_t>);
  static_assert(std::is_same_v<decltype(s.fieldText), std::string>);
  static_assert(std::is_same_v<decltype(s.cursor), size_t>);
  (void)s;
  return true;
}

#define RUN(c)                                        \
  do {                                                \
    if (!c()) {                                       \
      std::printf("FAIL %s\n", #c);                   \
      failures++;                                     \
    } else {                                          \
      std::printf("PASS %s\n", #c);                   \
    }                                                 \
  } while (0)

int main() {
  int failures = 0;
  RUN(check_scale_up_preserves_state);
  RUN(check_scale_down_preserves_state);
  RUN(check_empty_field_preserved);
  RUN(check_cursor_at_end_preserved);
  RUN(check_density_variation_preserves_state);
  RUN(check_repeated_apply_is_stable);
  RUN(check_large_scrollpos_preserved);
  RUN(check_out_of_range_scale_preserves_state);
  RUN(check_signatures_match_plan);
  if (failures == 0) std::printf("scale_config_test: all passing\n");
  return failures == 0 ? 0 : 1;
}
