// T8 contract tests — FR-LAYOUT-012 / AC-LAYOUT-034 (O(1) safe-area query
// with no tree walk).
// Maps to TC-LAYOUT-034 (`engine/layout/tests/safe_area_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services, no JS calls. Consumes (does not
// reimplement) the specs-state-reactivity.md inset channel (this suite only
// observes the `queryInset`/`revision` polling surface owned by T6, plus the
// T7 `onImeFrame` feed for the Ime-exclusion case).
// Binds ONLY to the T8 `Interface:` line (reproduced verbatim in
// unit-tests/T8.md):
//   `EdgeInsets safeArea() const noexcept;`
//   — cached composite of systemBars + displayCutout + navigationBars,
//   recomputed only on inset revision bump; error shape: none (cached value,
//   noexcept, no allocation).
// The `InsetBus`/`EdgeInsets`/`InsetType` shapes are owned by T6's contract
// (unit-tests/T6.md); the `onImeFrame` feed shape is owned by T7's contract
// (unit-tests/T7.md); this suite consumes them.
//
// ASSUMPTION (composite rule): per-edge UNION of the three contributors —
// each edge of `safeArea()` is the component-wise MAXIMUM of the same edge
// across SystemBars, DisplayCutout, and NavigationBars. The Interface line
// pins the contributor set, not the combine operator; max is the defensible
// reading (safe area is the union that keeps content clear of every
// contributor; summation would double-count overlapping bars/cutout).
// ASSUMPTION (exclusion set): Ime, TappableElement, SystemGestures,
// MandatorySystemGestures, and CaptionBar never contribute — an Ime-only
// change leaves `safeArea()` bit-identical.
// ASSUMPTION (initial state): a fresh bus reads zero safe area (all three
// contributors start zeroed per T6's origin assumption).
// ASSUMPTION (cache observability): "recomputed only on inset revision bump"
// and "never walks the tree" are asserted structurally through observable
// behavior only — (a) repeated `safeArea()` calls with no intervening write
// return identical values (cache-hit path); (b) `safeArea()` is const and
// never bumps any type's `revision()` (no side effects, no traversal
// bookkeeping); (c) writes to non-contributors (Ime) leave the value
// unchanged while the next contributing write is reflected immediately.
// No private fields, timings, or allocation counters are asserted.
// ASSUMPTION (float compare): epsilon `1e-4f`; fixtures exactly
// representable.
#include <cstdint>
#include <cstdio>
#include <utility>

#include "layout/insets.h"

namespace {

constexpr float kEps = 1e-4f;

bool near(float a, float b) { return (a > b ? a - b : b - a) <= kEps; }

bool insetsNear(const layout::EdgeInsets& a, const layout::EdgeInsets& b) {
  return near(a.left, b.left) && near(a.top, b.top) &&
         near(a.right, b.right) && near(a.bottom, b.bottom);
}

bool insetsZero(const layout::EdgeInsets& e) {
  return near(e.left, 0.0f) && near(e.top, 0.0f) &&
         near(e.right, 0.0f) && near(e.bottom, 0.0f);
}

float pickMax(float a, float b, float c) {
  float m = a > b ? a : b;
  return m > c ? m : c;
}

layout::EdgeInsets maxComposite(const layout::EdgeInsets& bars,
                                const layout::EdgeInsets& cutout,
                                const layout::EdgeInsets& nav) {
  return layout::EdgeInsets{
      pickMax(bars.left, cutout.left, nav.left),
      pickMax(bars.top, cutout.top, nav.top),
      pickMax(bars.right, cutout.right, nav.right),
      pickMax(bars.bottom, cutout.bottom, nav.bottom),
  };
}

void setAll(layout::InsetBus& bus, layout::InsetType t,
            const layout::EdgeInsets& e) {
  bus.setInset(t, e);
}

bool revisionsUnchangedByQuery(const layout::InsetBus& bus) {
  const layout::InsetType all[] = {
      layout::InsetType::SystemBars,
      layout::InsetType::DisplayCutout,
      layout::InsetType::Ime,
      layout::InsetType::TappableElement,
      layout::InsetType::SystemGestures,
      layout::InsetType::MandatorySystemGestures,
      layout::InsetType::CaptionBar,
      layout::InsetType::NavigationBars,
  };
  std::uint64_t before[8];
  for (int i = 0; i < 8; ++i) before[i] = bus.revision(all[i]);
  for (int i = 0; i < 16; ++i) {
    volatile float sink = 0.0f;
    const layout::EdgeInsets s = bus.safeArea();
    sink += s.left + s.top + s.right + s.bottom;
    (void)sink;
  }
  for (int i = 0; i < 8; ++i) {
    if (bus.revision(all[i]) != before[i]) return false;
  }
  return true;
}

}  // namespace

// Compile-time: the T8 snapshot surface never throws and never allocates
// (returned by value; no allocation on the const path).
static_assert(noexcept(std::declval<const layout::InsetBus&>().safeArea()),
              "safeArea must be noexcept");

// Named case: a fresh bus reads zero safe area.
static bool check_fresh_bus_safe_area_zero() {
  layout::InsetBus bus;
  return insetsZero(bus.safeArea());
}

// Named case (TC-LAYOUT-034 core): safe area is the per-edge max composite
// of systemBars + displayCutout + navigationBars, with each edge provable
// from a different contributor.
static bool check_composite_is_per_edge_max_of_three_contributors() {
  layout::InsetBus bus;
  const layout::EdgeInsets bars{10.0f, 20.0f, 5.0f, 0.0f};
  const layout::EdgeInsets cutout{4.0f, 30.0f, 6.0f, 0.0f};
  const layout::EdgeInsets nav{2.0f, 8.0f, 12.0f, 40.0f};
  setAll(bus, layout::InsetType::SystemBars, bars);
  setAll(bus, layout::InsetType::DisplayCutout, cutout);
  setAll(bus, layout::InsetType::NavigationBars, nav);
  const layout::EdgeInsets expected = maxComposite(bars, cutout, nav);
  // Spot-check the union: left from bars, top from cutout, right/bottom
  // from nav — no single contributor explains the whole result.
  if (!near(expected.left, 10.0f) || !near(expected.top, 30.0f) ||
      !near(expected.right, 12.0f) || !near(expected.bottom, 40.0f)) {
    return false;
  }
  return insetsNear(bus.safeArea(), expected);
}

// Named case: each contributor alone drives the composite (others zero).
static bool check_each_contributor_alone_drives_composite() {
  {
    layout::InsetBus bus;
    const layout::EdgeInsets bars{1.0f, 2.0f, 3.0f, 4.0f};
    setAll(bus, layout::InsetType::SystemBars, bars);
    if (!insetsNear(bus.safeArea(), bars)) return false;
  }
  {
    layout::InsetBus bus;
    const layout::EdgeInsets cutout{5.0f, 6.0f, 7.0f, 8.0f};
    setAll(bus, layout::InsetType::DisplayCutout, cutout);
    if (!insetsNear(bus.safeArea(), cutout)) return false;
  }
  {
    layout::InsetBus bus;
    const layout::EdgeInsets nav{9.0f, 1.0f, 2.0f, 3.0f};
    setAll(bus, layout::InsetType::NavigationBars, nav);
    if (!insetsNear(bus.safeArea(), nav)) return false;
  }
  return true;
}

// Named case: Ime frames never leak into the safe area — the keyboard can
// animate while the safe-area snapshot stays bit-identical.
static bool check_ime_frames_never_change_safe_area() {
  layout::InsetBus bus;
  setAll(bus, layout::InsetType::SystemBars,
         layout::EdgeInsets{10.0f, 20.0f, 10.0f, 0.0f});
  setAll(bus, layout::InsetType::NavigationBars,
         layout::EdgeInsets{0.0f, 0.0f, 0.0f, 30.0f});
  const layout::EdgeInsets pinned = bus.safeArea();
  for (std::uint64_t i = 1; i <= 16; ++i) {
    bus.onImeFrame(15.0f * static_cast<float>(i), i);
    if (!insetsNear(bus.safeArea(), pinned)) return false;
  }
  bus.setInset(layout::InsetType::Ime, layout::EdgeInsets{1.0f, 2.0f, 3.0f, 200.0f});
  return insetsNear(bus.safeArea(), pinned);
}

// Named case: non-contributing types (gestures, tappable, caption bar)
// never move the composite.
static bool check_non_contributing_types_ignored() {
  layout::InsetBus bus;
  setAll(bus, layout::InsetType::SystemBars,
         layout::EdgeInsets{10.0f, 20.0f, 10.0f, 10.0f});
  const layout::EdgeInsets pinned = bus.safeArea();
  setAll(bus, layout::InsetType::SystemGestures,
         layout::EdgeInsets{50.0f, 50.0f, 50.0f, 50.0f});
  setAll(bus, layout::InsetType::MandatorySystemGestures,
         layout::EdgeInsets{60.0f, 60.0f, 60.0f, 60.0f});
  setAll(bus, layout::InsetType::TappableElement,
         layout::EdgeInsets{70.0f, 70.0f, 70.0f, 70.0f});
  setAll(bus, layout::InsetType::CaptionBar,
         layout::EdgeInsets{80.0f, 80.0f, 80.0f, 80.0f});
  return insetsNear(bus.safeArea(), pinned);
}

// Named case (cache-hit path): with no intervening write, repeated queries
// return identical values — the cached struct serves every hit.
static bool check_repeated_queries_without_writes_are_identical() {
  layout::InsetBus bus;
  setAll(bus, layout::InsetType::SystemBars,
         layout::EdgeInsets{3.0f, 5.0f, 3.0f, 0.0f});
  setAll(bus, layout::InsetType::DisplayCutout,
         layout::EdgeInsets{0.0f, 12.0f, 0.0f, 0.0f});
  const layout::EdgeInsets first = bus.safeArea();
  for (int i = 0; i < 64; ++i) {
    if (!insetsNear(bus.safeArea(), first)) return false;
  }
  return true;
}

// Named case (no tree walk, structural): querying the snapshot never bumps
// any revision and never mutates observable bus state.
static bool check_query_has_no_side_effects_on_revisions() {
  layout::InsetBus bus;
  setAll(bus, layout::InsetType::SystemBars,
         layout::EdgeInsets{3.0f, 5.0f, 3.0f, 0.0f});
  setAll(bus, layout::InsetType::NavigationBars,
         layout::EdgeInsets{0.0f, 0.0f, 0.0f, 7.0f});
  return revisionsUnchangedByQuery(bus);
}

// Named case (recompute on revision bump): the next contributing write is
// reflected immediately — last-write-wins flows through the cache.
static bool check_contributing_write_reflected_immediately() {
  layout::InsetBus bus;
  setAll(bus, layout::InsetType::SystemBars,
         layout::EdgeInsets{10.0f, 10.0f, 10.0f, 10.0f});
  const layout::EdgeInsets first{10.0f, 10.0f, 10.0f, 10.0f};
  if (!insetsNear(bus.safeArea(), first)) return false;
  setAll(bus, layout::InsetType::DisplayCutout,
         layout::EdgeInsets{0.0f, 25.0f, 0.0f, 0.0f});
  const layout::EdgeInsets second{10.0f, 25.0f, 10.0f, 10.0f};
  if (!insetsNear(bus.safeArea(), second)) return false;
  // Lowering a contributor below a rival edge exposes the rival (max, not
  // sticky cache): bars drop to zero, cutout top still dominates.
  setAll(bus, layout::InsetType::SystemBars,
         layout::EdgeInsets{0.0f, 0.0f, 0.0f, 0.0f});
  const layout::EdgeInsets third{0.0f, 25.0f, 0.0f, 0.0f};
  return insetsNear(bus.safeArea(), third);
}

// Named case: extreme contributor values never throw and stay queryable.
static bool check_extreme_values_never_throw_and_stay_queryable() {
  layout::InsetBus bus;
  bool threw = false;
  try {
    setAll(bus, layout::InsetType::SystemBars,
           layout::EdgeInsets{-1.0e9f, 1.0e9f, -0.0f, 1.0e30f});
    (void)bus.safeArea();
    (void)bus.revision(layout::InsetType::SystemBars);
    setAll(bus, layout::InsetType::SystemBars,
           layout::EdgeInsets{0.0f, 0.0f, 0.0f, 0.0f});
    (void)bus.safeArea();
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  return insetsZero(bus.safeArea());
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
  RUN_CASE(check_fresh_bus_safe_area_zero);
  RUN_CASE(check_composite_is_per_edge_max_of_three_contributors);
  RUN_CASE(check_each_contributor_alone_drives_composite);
  RUN_CASE(check_ime_frames_never_change_safe_area);
  RUN_CASE(check_non_contributing_types_ignored);
  RUN_CASE(check_repeated_queries_without_writes_are_identical);
  RUN_CASE(check_query_has_no_side_effects_on_revisions);
  RUN_CASE(check_contributing_write_reflected_immediately);
  RUN_CASE(check_extreme_values_never_throw_and_stay_queryable);
  if (failures > 0) {
    std::printf("safe_area_test: %d failing\n", failures);
    return 1;
  }
  std::printf("safe_area_test: all passing\n");
  return 0;
}
