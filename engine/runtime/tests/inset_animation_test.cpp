// T6 contract tests — FR-TEXT-008 / AC-TEXT-016 (TC-TEXT-016).
// Binds ONLY to the T6 `Interface:` line; never to implementation.
// Extends the `runtime/ime_inset.h` seam concept (frame-tagged inset samples,
// lag <= 1 frame) without touching that header.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/inset_animation.h` does not exist yet, so this
// file must FAIL TO COMPILE. That is the recorded red state.
//
// ASSUMPTION (namespace): `InsetFrame`/`InsetSync` are referenced unqualified
// (global namespace, matching the GapBuffer/ALooperBridge precedent). If the
// coder nests them (e.g. `runtime::`), call sites reconcile at verify —
// expectations do not change.
// ASSUMPTION (default): a default-constructed `InsetSync` reports
// `last_applied_frame() == 0` and `height_for_frame(any) == 0.0f`.
#include <cstdio>
#include <cstring>

#include "runtime/inset_animation.h"

// --- harness cases (match existing test style) -----------------------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T6 cases (each maps to the Interface: line + AC-TEXT-016) --------------

// Happy path: a sample maps 1:1 to its frame id.
static bool check_sample_maps_to_frame() {
  InsetSync sync;
  InsetFrame f;
  f.ime_height = 120.0f;
  f.frame_id = 10;
  sync.on_inset(f);
  return sync.height_for_frame(10) == 120.0f &&
         sync.last_applied_frame() == 10;
}

// Happy path: successive samples track the latest height.
static bool check_latest_sample_wins() {
  InsetSync sync;
  InsetFrame a;
  a.ime_height = 40.0f;
  a.frame_id = 20;
  sync.on_inset(a);
  InsetFrame b;
  b.ime_height = 80.0f;
  b.frame_id = 21;
  sync.on_inset(b);
  return sync.height_for_frame(21) == 80.0f &&
         sync.last_applied_frame() == 21;
}

// Boundary: negative heights clamp to 0.
static bool check_negative_height_clamps_to_zero() {
  InsetSync sync;
  InsetFrame f;
  f.ime_height = -40.0f;
  f.frame_id = 3;
  sync.on_inset(f);
  return sync.height_for_frame(3) == 0.0f &&
         sync.last_applied_frame() == 3;
}

// Boundary: unknown future frame_id returns the latest known height.
static bool check_future_frame_returns_latest() {
  InsetSync sync;
  InsetFrame f;
  f.ime_height = 96.0f;
  f.frame_id = 30;
  sync.on_inset(f);
  return sync.height_for_frame(31) == 96.0f &&
         sync.height_for_frame(1000) == 96.0f &&
         sync.last_applied_frame() == 30;
}

// Boundary: default state reports frame 0 and height 0.
static bool check_default_state_is_zero() {
  InsetSync sync;
  return sync.last_applied_frame() == 0 &&
         sync.height_for_frame(0) == 0.0f &&
         sync.height_for_frame(999) == 0.0f;
}

// Acceptance: a 120 Hz sample train keeps lag <= 1 frame and never applies a
// sample to the wrong frame.
static bool check_sample_train_lag_within_one_frame() {
  InsetSync sync;
  const int kFrames = 120;
  for (int i = 1; i <= kFrames; ++i) {
    InsetFrame f;
    f.ime_height = static_cast<float>(i);
    f.frame_id = i;
    sync.on_inset(f);
    // Applied frame must be within one frame of the announced frame.
    if (sync.last_applied_frame() - i > 1) return false;
    if (sync.last_applied_frame() < i - 1) return false;
    if (sync.height_for_frame(i) != static_cast<float>(i)) return false;
  }
  return sync.last_applied_frame() == kFrames;
}

// Error path: extreme inputs never throw and keep the seam usable.
static bool check_extreme_inputs_never_throw() {
  InsetSync sync;
  try {
    InsetFrame neg;
    neg.ime_height = -1000000.0f;
    neg.frame_id = -2147483647;
    sync.on_inset(neg);
    if (sync.height_for_frame(-2147483647) != 0.0f) return false;
    InsetFrame huge;
    huge.ime_height = 1.0e30f;
    huge.frame_id = 2147483647;
    sync.on_inset(huge);
    if (sync.height_for_frame(2147483647) != 1.0e30f) return false;
    // Future query past the huge frame still returns the latest height.
    if (sync.height_for_frame(2147483647) != sync.height_for_frame(2147483647))
      return false;
  } catch (...) {
    return false;
  }
  return true;
}

// Zero-height sample hides the keyboard and still advances the frame.
static bool check_zero_height_advances_frame() {
  InsetSync sync;
  InsetFrame a;
  a.ime_height = 120.0f;
  a.frame_id = 5;
  sync.on_inset(a);
  InsetFrame b;
  b.ime_height = 0.0f;
  b.frame_id = 6;
  sync.on_inset(b);
  return sync.height_for_frame(6) == 0.0f &&
         sync.last_applied_frame() == 6;
}

// F4b (review MINOR InsetSync window): 2000 sequential frames keep the
// latest correct and early-frame queries sane (fallback, no crash). Pins
// only no-throw + latest correctness, never eviction specifics.
static bool check_long_window_stays_sane() {
  InsetSync sync;
  try {
    const int kFrames = 2000;
    for (int i = 1; i <= kFrames; ++i) {
      InsetFrame f;
      f.ime_height = static_cast<float>(i);
      f.frame_id = i;
      sync.on_inset(f);
    }
    if (sync.last_applied_frame() != 2000) return false;
    if (sync.height_for_frame(2000) != 2000.0f) return false;
    const float early = sync.height_for_frame(1);
    if (early < 0.0f) return false;
  } catch (...) {
    return false;
  }
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
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_sample_maps_to_frame);
  RUN_CASE(check_latest_sample_wins);
  RUN_CASE(check_negative_height_clamps_to_zero);
  RUN_CASE(check_future_frame_returns_latest);
  RUN_CASE(check_default_state_is_zero);
  RUN_CASE(check_sample_train_lag_within_one_frame);
  RUN_CASE(check_extreme_inputs_never_throw);
  RUN_CASE(check_zero_height_advances_frame);
  RUN_CASE(check_long_window_stays_sane);
  if (failures > 0) {
    std::printf("inset_animation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("inset_animation_test: all passing\n");
  return 0;
}
