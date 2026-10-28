// T2 contract tests — FR-LAYOUT-014 / AC-LAYOUT-040 (fold state,
// orientation, and occlusion are available as signals and update on
// layout-change callbacks).
// Maps to TC-LAYOUT-040 (`engine/layout/tests/fold_signal_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services, no JS calls. Consumes (does not
// reimplement) the specs-state-reactivity.md signal engine: this suite only
// observes the FoldSignal polling surface (`revision()`), never the engine
// internals.
// Binds ONLY to the T2 `Interface:` line (reproduced verbatim in
// unit-tests/T2.md); the `FoldingFeatureState` input shape is owned by T1's
// contract (unit-tests/T1.md).
//
// ASSUMPTION (enumerators): same naming assumption as T1 —
// FoldState::Flat/HalfOpened/Unknown, FoldOrientation::Vertical/Horizontal,
// OcclusionType::None/Full (PascalCase per layout/form_factor.h convention;
// spec semantics from FoldingFeature.State/OcclusionType).
// ASSUMPTION (initial revision): a default-constructed FoldSignal starts at
// revision 0 (the Interface line pins monotonicity, not the origin).
// ASSUMPTION (bump-on-every-update): every update() call bumps revision by
// exactly 1, even for a byte-identical state — this is what "refresh on every
// layout-change callback" (AC-LAYOUT-040) requires. If the coder coalesces
// identical updates, this case is updated only via planner routing, not
// silently.
// ASSUMPTION (Rect): `layout::Rect` aggregate of four floats read as
// x/y/w/h (owned by T1; repeated here because update() takes the T1 state).
#include <cstdint>
#include <cstdio>
#include <utility>

#include "layout/fold_signal.h"
#include "layout/form_factor.h"

namespace {

layout::FoldingFeatureState flat_feature() {
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::Flat;
  s.orientation = layout::FoldOrientation::Vertical;
  s.occlusion = layout::OcclusionType::None;
  s.hingeAngleDeg = 180.0f;
  s.hasHingeAngle = true;
  s.bounds = layout::Rect{0.0f, 0.0f, 0.0f, 0.0f};
  return s;
}

layout::FoldingFeatureState half_opened_feature() {
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::HalfOpened;
  s.orientation = layout::FoldOrientation::Horizontal;
  s.occlusion = layout::OcclusionType::Full;
  s.hingeAngleDeg = 90.0f;
  s.hasHingeAngle = true;
  s.bounds = layout::Rect{0.0f, 390.0f, 720.0f, 20.0f};
  return s;
}

}  // namespace

// Compile-time: the whole FoldSignal surface never throws (the Interface
// line pins `noexcept throughout`).
static_assert(noexcept(std::declval<layout::FoldSignal&>().update(
                  std::declval<const layout::FoldingFeatureState&>())),
              "update must be noexcept");
static_assert(noexcept(std::declval<const layout::FoldSignal&>().state()),
              "state must be noexcept");
static_assert(
    noexcept(std::declval<const layout::FoldSignal&>().orientation()),
    "orientation must be noexcept");
static_assert(noexcept(std::declval<const layout::FoldSignal&>().occlusion()),
              "occlusion must be noexcept");
static_assert(noexcept(std::declval<const layout::FoldSignal&>().revision()),
              "revision must be noexcept");

// Named case: a fresh signal starts at revision 0.
static bool check_initial_revision_is_zero() {
  layout::FoldSignal sig;
  return sig.revision() == 0u;
}

// Named case: one layout-change update publishes fold state, orientation,
// and occlusion as readable signals and bumps the revision.
static bool check_update_publishes_state_orientation_occlusion() {
  layout::FoldSignal sig;
  sig.update(half_opened_feature());
  return sig.state() == layout::FoldState::HalfOpened &&
         sig.orientation() == layout::FoldOrientation::Horizontal &&
         sig.occlusion() == layout::OcclusionType::Full &&
         sig.revision() == 1u;
}

// Named case (AC-LAYOUT-040 "refresh on every layout-change callback"):
// every update() bumps the revision — including a byte-identical repeat —
// so subscribers polling revision() observe every callback.
static bool check_each_update_bumps_revision_including_identical() {
  layout::FoldSignal sig;
  sig.update(flat_feature());
  if (sig.revision() != 1u) return false;
  sig.update(half_opened_feature());
  if (sig.revision() != 2u) return false;
  sig.update(half_opened_feature());  // identical repeat still refreshes.
  return sig.revision() == 3u;
}

// Named case: revision is monotonic over a long scripted callback sequence.
static bool check_revision_monotonic_over_long_sequence() {
  layout::FoldSignal sig;
  std::uint64_t prev = sig.revision();
  for (int i = 0; i < 100; ++i) {
    sig.update(i % 2 == 0 ? flat_feature() : half_opened_feature());
    const std::uint64_t cur = sig.revision();
    if (cur <= prev) return false;
    prev = cur;
  }
  return prev == 100u;
}

// Named case (last-write-wins): after conflicting updates, the signals
// reflect the latest write.
static bool check_last_write_wins() {
  layout::FoldSignal sig;
  sig.update(half_opened_feature());
  sig.update(flat_feature());
  return sig.state() == layout::FoldState::Flat &&
         sig.orientation() == layout::FoldOrientation::Vertical &&
         sig.occlusion() == layout::OcclusionType::None;
}

// Named case: a sensor-absent update (no hinge angle; bounds + occlusion
// only, per the T1 degrade rule) still refreshes all three signals.
static bool check_sensor_absent_update_refreshes_signals() {
  layout::FoldSignal sig;
  layout::FoldingFeatureState s{};
  s.state = layout::FoldState::HalfOpened;
  s.orientation = layout::FoldOrientation::Vertical;
  s.occlusion = layout::OcclusionType::Full;
  s.hasHingeAngle = false;
  s.bounds = layout::Rect{350.0f, 0.0f, 20.0f, 800.0f};
  sig.update(s);
  return sig.state() == layout::FoldState::HalfOpened &&
         sig.orientation() == layout::FoldOrientation::Vertical &&
         sig.occlusion() == layout::OcclusionType::Full &&
         sig.revision() == 1u;
}

// Named case: extreme/malformed inputs never throw and leave the signals
// queryable (never-throws half of the contract under hostile input).
static bool check_extreme_updates_never_throw_and_stay_queryable() {
  layout::FoldSignal sig;
  bool threw = false;
  try {
    layout::FoldingFeatureState s = flat_feature();
    s.hingeAngleDeg = -30.0f;
    s.bounds = layout::Rect{-1.0e9f, -1.0e9f, 1.0e9f, 1.0e9f};
    sig.update(s);
    (void)sig.state();
    (void)sig.revision();
    layout::FoldingFeatureState unknown{};
    unknown.state = layout::FoldState::Unknown;
    unknown.hasHingeAngle = false;
    sig.update(unknown);
    (void)sig.state();
    (void)sig.orientation();
    (void)sig.occlusion();
    (void)sig.revision();
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  if (sig.revision() != 2u) return false;
  return sig.state() == layout::FoldState::Unknown;
}

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
  RUN_CASE(check_initial_revision_is_zero);
  RUN_CASE(check_update_publishes_state_orientation_occlusion);
  RUN_CASE(check_each_update_bumps_revision_including_identical);
  RUN_CASE(check_revision_monotonic_over_long_sequence);
  RUN_CASE(check_last_write_wins);
  RUN_CASE(check_sensor_absent_update_refreshes_signals);
  RUN_CASE(check_extreme_updates_never_throw_and_stay_queryable);
  if (failures > 0) {
    std::printf("fold_signal_test: %d failing\n", failures);
    return 1;
  }
  std::printf("fold_signal_test: all passing\n");
  return 0;
}
