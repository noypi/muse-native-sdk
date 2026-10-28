// T7 contract tests — FR-LAYOUT-012 / AC-LAYOUT-033 (scripted IME open
// animation advances the IME inset monotonically per frame with matching
// frameIds, no jumps).
// Maps to TC-LAYOUT-033 (`engine/layout/tests/ime_animation_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services. Consumes (does not reimplement) the
// specs-state-reactivity.md inset channel (observes `queryInset(Ime)` +
// per-type `revision`), the specs-typography-text.md InputConnection bridge
// (test fakes the bridge side with scripted `onImeFrame` calls — no live
// bridge), and the render-graph vsync (scripted frameIds stand in for
// vsync; no timers, no threads).
// Binds ONLY to the T7 `Interface:` line (reproduced verbatim in
// unit-tests/T7.md):
//   `void InsetBus::onImeFrame(float imeBottom, uint64_t frameId) noexcept;
//    uint64_t imeFrameId() const noexcept;`
//   — frames applied atomically per vsync; stale frameId ignored; noexcept.
// The `InsetBus`/`EdgeInsets`/`InsetType` shapes are owned by T6's contract
// (unit-tests/T6.md); this suite consumes them.
//
// ASSUMPTION (frame slot): `onImeFrame(b, id)` writes exactly the bottom
// edge of the `InsetType::Ime` slot (`{0, 0, 0, b}`) and bumps the Ime
// revision — the defensible reading of "frames applied atomically per
// vsync" on the T6 bus. No other inset type is touched.
// ASSUMPTION (stale rule): a frame whose id is less than OR EQUAL to the
// current `imeFrameId()` is stale and ignored entirely (no inset change, no
// revision bump, no frameId change). Equality counts as stale so a
// redelivered vsync cannot double-apply.
// ASSUMPTION (initial state): a fresh bus reads `imeFrameId() == 0`, zero
// Ime insets, and Ime revision 0.
// ASSUMPTION (no-jump check): the scripted open moves 0 -> 240px in equal
// per-frame steps; a "jump" is any single-frame delta exceeding 1.5x the
// scripted step. The Interface line pins monotonicity + matching frameIds,
// not the step size; this bound only guards the scripted sequence.
#include <cstdint>
#include <cstdio>
#include <utility>

#include "layout/insets.h"

namespace {

constexpr float kEps = 1e-4f;

bool near(float a, float b) { return (a > b ? a - b : b - a) <= kEps; }

float imeBottom(const layout::InsetBus& bus) {
  return bus.queryInset(layout::InsetType::Ime).bottom;
}

}  // namespace

// Compile-time: the T7 frame surface never throws.
static_assert(noexcept(std::declval<layout::InsetBus&>().onImeFrame(
                  std::declval<float>(), std::declval<std::uint64_t>())),
              "onImeFrame must be noexcept");
static_assert(noexcept(std::declval<const layout::InsetBus&>().imeFrameId()),
              "imeFrameId must be noexcept");

// Named case: a fresh bus starts at frame 0 with zero IME inset.
static bool check_initial_frame_zero_and_ime_zero() {
  layout::InsetBus bus;
  if (bus.imeFrameId() != 0u) return false;
  const layout::EdgeInsets e = bus.queryInset(layout::InsetType::Ime);
  return near(e.left, 0.0f) && near(e.top, 0.0f) && near(e.right, 0.0f) &&
         near(e.bottom, 0.0f);
}

// Named case (TC-LAYOUT-033 core): a scripted IME open (0 -> 240px over 16
// frames) advances the IME bottom monotonically with matching frameIds and
// no jumps.
static bool check_scripted_open_advances_monotonically_no_jumps() {
  layout::InsetBus bus;
  constexpr int kFrames = 16;
  constexpr float kTarget = 240.0f;
  constexpr float kStep = kTarget / static_cast<float>(kFrames);
  float prev = 0.0f;
  for (int i = 1; i <= kFrames; ++i) {
    const float bottom = kStep * static_cast<float>(i);
    bus.onImeFrame(bottom, static_cast<std::uint64_t>(i));
    if (bus.imeFrameId() != static_cast<std::uint64_t>(i)) return false;
    const float cur = imeBottom(bus);
    if (!near(cur, bottom)) return false;
    if (!(cur > prev || near(cur, prev))) return false;  // monotonic
    if (cur - prev > 1.5f * kStep + kEps) return false;  // no jump
    prev = cur;
  }
  return near(imeBottom(bus), kTarget);
}

// Named case (atomic per vsync): each frame lands exactly — a single
// onImeFrame call publishes the full bottom value with its frameId, and
// only the bottom edge of the Ime slot moves.
static bool check_frames_applied_atomically_per_vsync() {
  layout::InsetBus bus;
  bus.onImeFrame(120.0f, 7u);
  if (bus.imeFrameId() != 7u) return false;
  const layout::EdgeInsets e = bus.queryInset(layout::InsetType::Ime);
  if (!near(e.bottom, 120.0f)) return false;
  if (!near(e.left, 0.0f) || !near(e.top, 0.0f) || !near(e.right, 0.0f)) {
    return false;
  }
  // No other inset type is touched by an IME frame.
  const layout::EdgeInsets bars =
      bus.queryInset(layout::InsetType::SystemBars);
  return near(bars.left, 0.0f) && near(bars.top, 0.0f) &&
         near(bars.right, 0.0f) && near(bars.bottom, 0.0f);
}

// Named case (stale ignored): an older frameId changes nothing — inset,
// frameId, and Ime revision all stay put.
static bool check_stale_frame_id_ignored() {
  layout::InsetBus bus;
  bus.onImeFrame(100.0f, 10u);
  const std::uint64_t rev = bus.revision(layout::InsetType::Ime);
  bus.onImeFrame(200.0f, 5u);  // stale: older id
  if (bus.imeFrameId() != 10u) return false;
  if (!near(imeBottom(bus), 100.0f)) return false;
  return bus.revision(layout::InsetType::Ime) == rev;
}

// Named case (duplicate redelivery ignored): repeating the current frameId
// is stale — no double-apply, no revision bump.
static bool check_duplicate_frame_id_ignored() {
  layout::InsetBus bus;
  bus.onImeFrame(80.0f, 4u);
  const std::uint64_t rev = bus.revision(layout::InsetType::Ime);
  bus.onImeFrame(999.0f, 4u);  // same id redelivered
  if (bus.imeFrameId() != 4u) return false;
  if (!near(imeBottom(bus), 80.0f)) return false;
  return bus.revision(layout::InsetType::Ime) == rev;
}

// Named case: out-of-order late frame after progress is ignored.
static bool check_out_of_order_late_frame_ignored() {
  layout::InsetBus bus;
  for (std::uint64_t i = 1; i <= 8; ++i) {
    bus.onImeFrame(10.0f * static_cast<float>(i), i);
  }
  bus.onImeFrame(1000.0f, 3u);  // long-stale late arrival
  if (bus.imeFrameId() != 8u) return false;
  return near(imeBottom(bus), 80.0f);
}

// Named case: a fresh (greater) frameId after stale attempts still applies.
static bool check_fresh_frame_after_stale_still_applies() {
  layout::InsetBus bus;
  bus.onImeFrame(50.0f, 5u);
  bus.onImeFrame(60.0f, 2u);  // ignored
  bus.onImeFrame(70.0f, 6u);  // fresh: applies
  if (bus.imeFrameId() != 6u) return false;
  return near(imeBottom(bus), 70.0f);
}

// Named case: scripted open then close returns to zero monotonically with
// matching frameIds (close path mirrors the open path).
static bool check_scripted_close_returns_to_zero_monotonically() {
  layout::InsetBus bus;
  constexpr int kFrames = 8;
  constexpr float kTarget = 160.0f;
  for (int i = 1; i <= kFrames; ++i) {
    bus.onImeFrame(kTarget * static_cast<float>(i) / kFrames,
                   static_cast<std::uint64_t>(i));
  }
  float prev = imeBottom(bus);
  for (int i = 1; i <= kFrames; ++i) {
    const std::uint64_t id = static_cast<std::uint64_t>(kFrames + i);
    const float bottom = kTarget * (1.0f - static_cast<float>(i) / kFrames);
    bus.onImeFrame(bottom, id);
    if (bus.imeFrameId() != id) return false;
    const float cur = imeBottom(bus);
    if (!(cur < prev || near(cur, prev))) return false;
    prev = cur;
  }
  return near(imeBottom(bus), 0.0f);
}

// Named case: extreme frame values never throw and stay queryable.
static bool check_extreme_frames_never_throw_and_stay_queryable() {
  layout::InsetBus bus;
  bool threw = false;
  try {
    bus.onImeFrame(-50.0f, 1u);
    (void)imeBottom(bus);
    (void)bus.imeFrameId();
    bus.onImeFrame(1.0e30f, 2u);
    (void)imeBottom(bus);
    bus.onImeFrame(0.0f, 2u);  // stale (equal id): ignored, must not throw
    (void)bus.imeFrameId();
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  if (bus.imeFrameId() != 2u) return false;
  return near(imeBottom(bus), 1.0e30f);
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
  RUN_CASE(check_initial_frame_zero_and_ime_zero);
  RUN_CASE(check_scripted_open_advances_monotonically_no_jumps);
  RUN_CASE(check_frames_applied_atomically_per_vsync);
  RUN_CASE(check_stale_frame_id_ignored);
  RUN_CASE(check_duplicate_frame_id_ignored);
  RUN_CASE(check_out_of_order_late_frame_ignored);
  RUN_CASE(check_fresh_frame_after_stale_still_applies);
  RUN_CASE(check_scripted_close_returns_to_zero_monotonically);
  RUN_CASE(check_extreme_frames_never_throw_and_stay_queryable);
  if (failures > 0) {
    std::printf("ime_animation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ime_animation_test: all passing\n");
  return 0;
}
