// T1 — FR-TEXT-004 / AC-TEXT-007: C++ stylus sample batch pipeline.
// Hermetic harness: plain C++17, no test framework, no device, no JVM, no
// downloads. Cases drive only the public stylus interface with synthetic
// inline samples (no input-subsystem, no AMotionEvent, no JNI).
// Shape mirrors engine/text/tests/jni_free_path_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <type_traits>
#include <vector>

#include "muse/text/stylus.h"

// Plan Interface line (verbatim):
// struct StylusSample { float x, y, pressure, tiltX, tiltY, orientation;
//                       bool eraser; bool hover; };
// std::vector<StylusSample> ExpandSamples(const StylusSample& anchor,
//     const std::vector<StylusSample>& history,
//     const std::vector<StylusSample>& predicted);

static muse::text::StylusSample make_sample(float x, float y, float pressure,
                                            float tiltX, float tiltY,
                                            float orientation, bool eraser,
                                            bool hover) {
  muse::text::StylusSample s{};
  s.x = x;
  s.y = y;
  s.pressure = pressure;
  s.tiltX = tiltX;
  s.tiltY = tiltY;
  s.orientation = orientation;
  s.eraser = eraser;
  s.hover = hover;
  return s;
}

static bool samples_equal(const muse::text::StylusSample& a,
                          const muse::text::StylusSample& b) {
  return a.x == b.x && a.y == b.y && a.pressure == b.pressure &&
         a.tiltX == b.tiltX && a.tiltY == b.tiltY &&
         a.orientation == b.orientation && a.eraser == b.eraser &&
         a.hover == b.hover;
}

static bool check_expands_history_anchor_predicted_in_order() {
  const muse::text::StylusSample anchor =
      make_sample(10.0f, 20.0f, 0.5f, 0.1f, 0.2f, 0.3f, false, false);
  const std::vector<muse::text::StylusSample> history = {
      make_sample(1.0f, 2.0f, 0.1f, 0.0f, 0.0f, 0.0f, false, false),
      make_sample(5.0f, 6.0f, 0.3f, 0.05f, 0.06f, 0.07f, false, false),
  };
  const std::vector<muse::text::StylusSample> predicted = {
      make_sample(15.0f, 25.0f, 0.7f, 0.2f, 0.3f, 0.4f, false, false),
      make_sample(18.0f, 28.0f, 0.9f, 0.25f, 0.35f, 0.45f, false, false),
  };
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, history, predicted);
  if (out.size() != history.size() + 1 + predicted.size()) return false;
  if (!samples_equal(out[0], history[0])) return false;
  if (!samples_equal(out[1], history[1])) return false;
  if (!samples_equal(out[2], anchor)) return false;
  if (!samples_equal(out[3], predicted[0])) return false;
  if (!samples_equal(out[4], predicted[1])) return false;
  return true;
}

static bool check_all_axes_intact_per_sample() {
  const muse::text::StylusSample anchor =
      make_sample(11.5f, -3.25f, 0.77f, 0.61f, -0.42f, 1.9f, false, false);
  const std::vector<muse::text::StylusSample> history = {
      make_sample(-100.0f, 4000.0f, 0.01f, -1.0f, 1.0f, 3.14f, false, false),
  };
  const std::vector<muse::text::StylusSample> predicted = {
      make_sample(0.0f, 0.0f, 1.0f, 0.9f, 0.8f, -2.5f, false, false),
  };
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, history, predicted);
  if (out.size() != 3) return false;
  if (!samples_equal(out[0], history[0])) return false;
  if (!samples_equal(out[1], anchor)) return false;
  if (!samples_equal(out[2], predicted[0])) return false;
  return true;
}

static bool check_eraser_flag_preserved_per_sample() {
  const muse::text::StylusSample anchor =
      make_sample(1.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, true, false);
  const std::vector<muse::text::StylusSample> history = {
      make_sample(0.0f, 0.0f, 0.4f, 0.0f, 0.0f, 0.0f, false, false),
  };
  const std::vector<muse::text::StylusSample> predicted = {
      make_sample(2.0f, 2.0f, 0.6f, 0.0f, 0.0f, 0.0f, true, false),
  };
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, history, predicted);
  if (out.size() != 3) return false;
  if (out[0].eraser) return false;
  if (!out[1].eraser) return false;
  if (!out[2].eraser) return false;
  return true;
}

static bool check_hover_flag_preserved_per_sample() {
  const muse::text::StylusSample anchor =
      make_sample(1.0f, 1.0f, 0.0f, 0.1f, 0.1f, 0.0f, false, true);
  const std::vector<muse::text::StylusSample> history = {
      make_sample(0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.0f, false, true),
  };
  const std::vector<muse::text::StylusSample> predicted = {
      make_sample(2.0f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f, false, false),
  };
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, history, predicted);
  if (out.size() != 3) return false;
  if (!out[0].hover) return false;
  if (!out[1].hover) return false;
  if (out[2].hover) return false;
  // Hover samples carry zero pressure; it must survive untouched.
  if (out[0].pressure != 0.0f) return false;
  if (out[1].pressure != 0.0f) return false;
  return true;
}

static bool check_empty_history_and_predicted_yields_anchor_only() {
  const muse::text::StylusSample anchor =
      make_sample(7.0f, 8.0f, 0.5f, 0.0f, 0.0f, 0.0f, false, false);
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, {}, {});
  if (out.size() != 1) return false;
  return samples_equal(out[0], anchor);
}

static bool check_empty_history_keeps_anchor_first() {
  const muse::text::StylusSample anchor =
      make_sample(7.0f, 8.0f, 0.5f, 0.0f, 0.0f, 0.0f, false, false);
  const std::vector<muse::text::StylusSample> predicted = {
      make_sample(9.0f, 9.0f, 0.6f, 0.0f, 0.0f, 0.0f, false, false),
  };
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, {}, predicted);
  if (out.size() != 2) return false;
  if (!samples_equal(out[0], anchor)) return false;
  if (!samples_equal(out[1], predicted[0])) return false;
  return true;
}

static bool check_pressure_boundaries_preserved() {
  const muse::text::StylusSample anchor =
      make_sample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, false, false);
  const std::vector<muse::text::StylusSample> history = {
      make_sample(1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, false, false),
  };
  const std::vector<muse::text::StylusSample> out =
      muse::text::ExpandSamples(anchor, history, {});
  if (out.size() != 2) return false;
  if (out[0].pressure != 1.0f) return false;
  if (out[1].pressure != 0.0f) return false;
  return true;
}

static bool check_signature_takes_no_jvm_handle() {
  // AC-TEXT-007 unit-observable half: the batch entry point must be a pure
  // C++ free function with no JNIEnv/JavaVM parameter. Verified at compile
  // time. The "no per-sample JVM hop under a profiler" half is EXTERNAL.
  static_assert(std::is_same_v<decltype(&muse::text::ExpandSamples),
                               std::vector<muse::text::StylusSample> (*)(
                                   const muse::text::StylusSample&,
                                   const std::vector<muse::text::StylusSample>&,
                                   const std::vector<muse::text::StylusSample>&)>);
  // Struct carries exactly the plan's fields with the plan's types.
  static_assert(std::is_same_v<decltype(muse::text::StylusSample::x), float>);
  static_assert(std::is_same_v<decltype(muse::text::StylusSample::y), float>);
  static_assert(
      std::is_same_v<decltype(muse::text::StylusSample::pressure), float>);
  static_assert(
      std::is_same_v<decltype(muse::text::StylusSample::tiltX), float>);
  static_assert(
      std::is_same_v<decltype(muse::text::StylusSample::tiltY), float>);
  static_assert(
      std::is_same_v<decltype(muse::text::StylusSample::orientation), float>);
  static_assert(
      std::is_same_v<decltype(muse::text::StylusSample::eraser), bool>);
  static_assert(
      std::is_same_v<decltype(muse::text::StylusSample::hover), bool>);
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
  RUN_CASE(check_expands_history_anchor_predicted_in_order);
  RUN_CASE(check_all_axes_intact_per_sample);
  RUN_CASE(check_eraser_flag_preserved_per_sample);
  RUN_CASE(check_hover_flag_preserved_per_sample);
  RUN_CASE(check_empty_history_and_predicted_yields_anchor_only);
  RUN_CASE(check_empty_history_keeps_anchor_first);
  RUN_CASE(check_pressure_boundaries_preserved);
  RUN_CASE(check_signature_takes_no_jvm_handle);
  if (failures > 0) {
    std::printf("stylus_samples_test: %d failing\n", failures);
    return 1;
  }
  std::printf("stylus_samples_test: all passing\n");
  return 0;
}
