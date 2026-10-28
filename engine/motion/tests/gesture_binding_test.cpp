// T3 contract tests — FR-MOTION-006 / AC-MOTION-011, FR-MOTION-008 / AC-MOTION-019
// (TC-MOTION-011 + AC-MOTION-019).
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: explicit tMs injected per
// sample, no wall clock, no randomness.
// Binds to the T3 `Interface:` line (reproduced verbatim in unit-tests/T3.md).
//
// ASSUMPTION (names): the plan names `motion::GestureBinding`,
// `motion::InputSample{float position; float velocity; float tMs}` and
// `ITransformWriter::writeTransform(float value, float tMs)` but leaves the
// writer-injection and spring-observation spellings open. These tests inject
// the writer via `explicit GestureBinding(ITransformWriter* writer)` and read
// the release-seeded spring via `const SpringState& springState() const
// noexcept` (both in `motion::`). If the coder names the constructor/accessor
// differently, update call sites — not expectations — per reconciliation.
// Velocity units are units/s everywhere, matching the T1 solver state
// (SpringState::velocity) and the retarget epsilon (1e-3 units/s).
//
// ASSUMPTION (monotonicity): "non-monotonic tMs are ignored" is tested as
// strictly-increasing required: a sample with tMs <= last accepted tMs is
// ignored and the writer still holds the last value.
#include <cmath>
#include <cstdio>
#include <vector>

#include "motion/gesture_binding.h"
#include "motion/solver.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

class RecordingWriter : public motion::ITransformWriter {
 public:
  void writeTransform(float value, float tMs) override {
    values.push_back(value);
    times.push_back(tMs);
    lastValue = value;
    lastTMs = tMs;
    ++calls;
  }
  std::vector<float> values;
  std::vector<float> times;
  float lastValue = 0.0f;
  float lastTMs = 0.0f;
  int calls = 0;
};

motion::InputSample sample(float position, float velocity, float tMs) {
  motion::InputSample s;
  s.position = position;
  s.velocity = velocity;
  s.tMs = tMs;
  return s;
}

// Drag sample updates writer value in the same evaluate call (same tMs).
bool check_sample_writes_same_frame() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(42.0f, 1.0f, 100.0f));
  return w.calls == 1 && w.lastValue == 42.0f && w.lastTMs == 100.0f;
}

// Successive samples track the latest position with matching tMs.
bool check_samples_track_latest() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(10.0f, 0.0f, 0.0f));
  binding.onSample(sample(20.0f, 2.0f, 16.0f));
  binding.onSample(sample(30.0f, 3.0f, 32.0f));
  return w.calls == 3 && w.lastValue == 30.0f && w.lastTMs == 32.0f &&
         w.values[1] == 20.0f && w.times[1] == 16.0f;
}

// Non-monotonic tMs is ignored; writer still holds last value.
bool check_non_monotonic_sample_ignored() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(10.0f, 0.0f, 50.0f));
  binding.onSample(sample(99.0f, 5.0f, 40.0f));  // backwards in time
  if (w.calls != 1) return false;
  if (w.lastValue != 10.0f || w.lastTMs != 50.0f) return false;
  binding.onSample(sample(77.0f, 5.0f, 50.0f));  // equal tMs also ignored
  return w.calls == 1 && w.lastValue == 10.0f && w.lastTMs == 50.0f;
}

// Release with velocity v starts spring with initial velocity == v (1e-3).
bool check_release_seeds_spring_velocity() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(10.0f, 1.0f, 0.0f));
  binding.onSample(sample(25.0f, 4.5f, 16.0f));
  binding.onRelease(sample(25.0f, 4.5f, 16.0f));
  return near(binding.springState().velocity, 4.5f, 1e-3f);
}

// Release with zero velocity seeds a ~zero-velocity spring.
bool check_release_zero_velocity() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(60.0f, 0.0f, 100.0f));
  binding.onRelease(sample(60.0f, 0.0f, 100.0f));
  return near(binding.springState().velocity, 0.0f, 1e-3f);
}

// Release seeds spring value at the release position.
bool check_release_seeds_spring_value() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(15.0f, 2.0f, 8.0f));
  binding.onRelease(sample(33.0f, 2.0f, 24.0f));
  return near(binding.springState().value, 33.0f, 1e-3f);
}

// Negative release velocity is preserved (fling back).
bool check_release_negative_velocity() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  binding.onSample(sample(80.0f, -6.25f, 200.0f));
  binding.onRelease(sample(80.0f, -6.25f, 200.0f));
  return near(binding.springState().velocity, -6.25f, 1e-3f);
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_sample_writes_same_frame);
  RUN_CASE(check_samples_track_latest);
  RUN_CASE(check_non_monotonic_sample_ignored);
  RUN_CASE(check_release_seeds_spring_velocity);
  RUN_CASE(check_release_zero_velocity);
  RUN_CASE(check_release_seeds_spring_value);
  RUN_CASE(check_release_negative_velocity);
  if (failures > 0) {
    std::printf("gesture_binding_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gesture_binding_test: all passing\n");
  return 0;
}
