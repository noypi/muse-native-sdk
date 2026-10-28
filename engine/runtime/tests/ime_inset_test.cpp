// T3 contract tests — FR-RUNTIME-004 / AC-RUNTIME-010 (TC-RUNTIME-010
// hermetic half; device-lab ime-inset stays external).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds to the T3 `Interface:` line; struct/field names below are the test's
// proposal and the T3 contract records the reconciliation rule (update call
// sites, not expectations) if the coder names them differently.
//
// ASSUMPTION (types): `runtime::LayoutViewport` is default-constructible with
// public `float ime_inset_height` (current applied height, default 0) and
// public `int inset_announced_frame` (keyboard-announced frame, default 0).
// `inset_applied_frame(const LayoutViewport&)` reports the frame id of the
// last `apply_ime_inset` call.
#include <cmath>
#include <cstdio>
#include <limits>

#include "runtime/ime_inset.h"

namespace {

bool check_apply_records_applied_frame() {
  runtime::LayoutViewport v;
  v.inset_announced_frame = 10;
  runtime::apply_ime_inset(v, 100.0f, 10);
  return runtime::inset_applied_frame(v) == 10 &&
         v.ime_inset_height == 100.0f;
}

bool check_same_frame_satisfies_invariant() {
  runtime::LayoutViewport v;
  v.inset_announced_frame = 7;
  runtime::apply_ime_inset(v, 50.0f, 7);
  return runtime::inset_applied_frame(v) - v.inset_announced_frame <= 1;
}

bool check_next_frame_satisfies_invariant() {
  runtime::LayoutViewport v;
  v.inset_announced_frame = 7;
  runtime::apply_ime_inset(v, 50.0f, 8);
  return runtime::inset_applied_frame(v) - v.inset_announced_frame <= 1;
}

bool check_negative_inset_clamps_to_zero() {
  runtime::LayoutViewport v;
  v.inset_announced_frame = 3;
  runtime::apply_ime_inset(v, -40.0f, 3);
  return v.ime_inset_height == 0.0f &&
         runtime::inset_applied_frame(v) == 3;
}

bool check_zero_inset_hides_keyboard() {
  runtime::LayoutViewport v;
  v.inset_announced_frame = 5;
  runtime::apply_ime_inset(v, 120.0f, 5);
  runtime::apply_ime_inset(v, 0.0f, 6);
  return v.ime_inset_height == 0.0f &&
         runtime::inset_applied_frame(v) == 6;
}

bool check_animated_steps_stay_within_one_frame() {
  runtime::LayoutViewport v;
  const float heights[] = {0.0f, 40.0f, 80.0f, 120.0f};
  for (int i = 0; i < 4; ++i) {
    v.inset_announced_frame = 20 + i;
    runtime::apply_ime_inset(v, heights[i], 20 + i);
    if (runtime::inset_applied_frame(v) - v.inset_announced_frame > 1)
      return false;
    if (v.ime_inset_height != heights[i]) return false;
  }
  return true;
}

// T2 (BUG-RUNTIME-001 hardening): NaN clamps to 0, frame still recorded.
bool check_nan_inset_clamps_to_zero() {
  runtime::LayoutViewport v;
  runtime::apply_ime_inset(v, std::numeric_limits<float>::quiet_NaN(), 11);
  return v.ime_inset_height == 0.0f &&
         runtime::inset_applied_frame(v) == 11;
}

// T2: +Inf clamps to 0.0f (plan A3: no finite max defined), frame recorded.
bool check_positive_inf_inset_clamps_to_finite() {
  runtime::LayoutViewport v;
  runtime::apply_ime_inset(v, std::numeric_limits<float>::infinity(), 12);
  return v.ime_inset_height == 0.0f &&
         runtime::inset_applied_frame(v) == 12;
}

// T2: -Inf clamps to 0, frame still recorded.
bool check_negative_inf_inset_clamps_to_zero() {
  runtime::LayoutViewport v;
  runtime::apply_ime_inset(v, -std::numeric_limits<float>::infinity(), 13);
  return v.ime_inset_height == 0.0f &&
         runtime::inset_applied_frame(v) == 13;
}

}  // namespace

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
  RUN_CASE(check_apply_records_applied_frame);
  RUN_CASE(check_same_frame_satisfies_invariant);
  RUN_CASE(check_next_frame_satisfies_invariant);
  RUN_CASE(check_negative_inset_clamps_to_zero);
  RUN_CASE(check_zero_inset_hides_keyboard);
  RUN_CASE(check_animated_steps_stay_within_one_frame);
  RUN_CASE(check_nan_inset_clamps_to_zero);
  RUN_CASE(check_positive_inf_inset_clamps_to_finite);
  RUN_CASE(check_negative_inf_inset_clamps_to_zero);
  if (failures > 0) {
    std::printf("ime_inset_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ime_inset_test: all passing\n");
  return 0;
}
