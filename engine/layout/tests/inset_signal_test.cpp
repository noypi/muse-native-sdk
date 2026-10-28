// T6 contract tests — FR-LAYOUT-012 / AC-LAYOUT-032 (each inset type
// independently queryable per edge and subscribable as a signal with no
// cross-type leakage).
// Maps to TC-LAYOUT-032 (`engine/layout/tests/inset_signal_test.cpp`).
// Hermetic: C++17 + layout headers only; no network, no env/config reads,
// no filesystem, no live services, no JS calls. Consumes (does not
// reimplement) the specs-state-reactivity.md inset channel (this suite only
// observes the `queryInset`/`revision` polling surface) and the
// specs-input-gestures.md ALooper feed (test drives the bus directly with
// scripted values, never a live looper).
// Binds ONLY to the T6 `Interface:` line (reproduced verbatim in
// unit-tests/T6.md):
//   `enum class InsetType : uint8_t { SystemBars, DisplayCutout, Ime,
//    TappableElement, SystemGestures, MandatorySystemGestures, CaptionBar,
//    NavigationBars }; struct EdgeInsets { float left, top, right, bottom; };
//    EdgeInsets queryInset(InsetType t) const noexcept;
//    uint64_t revision(InsetType t) const noexcept;`
//   — per-type per-edge query + subscribable revision; zero insets for
//   unknown type; noexcept.
//
// ASSUMPTION (bus class): the query/revision methods live on
// `layout::InsetBus` (default-constructible), the T7-owned extension point
// (`InsetBus::onImeFrame`) names the class; T7 extends this same bus.
// ASSUMPTION (feed entry): scripted values enter via
// `void InsetBus::setInset(InsetType t, const EdgeInsets& e) noexcept` — the
// C++-side stand-in for the ALooper inset feed. The Interface line pins the
// polling surface, not the feed name; if the coder names it differently, only
// the call sites change (behavior expectations stand) via planner routing.
// ASSUMPTION (initial state): a default-constructed bus reads zero insets
// and revision 0 for every type (the Interface line pins zero-for-unknown,
// not the origin; zero-initial is the defensible reading).
// ASSUMPTION (revision rule): each `setInset` bumps that type's revision by
// exactly 1 and leaves every other type's revision untouched (this is what
// "subscribable as a signal with no cross-type leakage" requires).
// ASSUMPTION (unknown type): any enumerator value outside the eight named
// ones (e.g. `static_cast<InsetType>(255)`) reads zero insets and never
// throws; its revision query is likewise non-throwing.
// ASSUMPTION (Rect floats): per-edge floats are plain `float`s compared with
// an epsilon; fixture values are exactly representable.
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

constexpr layout::EdgeInsets kZero{0.0f, 0.0f, 0.0f, 0.0f};

constexpr layout::InsetType kAllTypes[] = {
    layout::InsetType::SystemBars,
    layout::InsetType::DisplayCutout,
    layout::InsetType::Ime,
    layout::InsetType::TappableElement,
    layout::InsetType::SystemGestures,
    layout::InsetType::MandatorySystemGestures,
    layout::InsetType::CaptionBar,
    layout::InsetType::NavigationBars,
};

}  // namespace

// Compile-time: the T6 polling surface never throws, the enum has the
// pinned underlying type, and EdgeInsets carries four per-edge floats.
static_assert(noexcept(std::declval<const layout::InsetBus&>().queryInset(
                  std::declval<layout::InsetType>())),
              "queryInset must be noexcept");
static_assert(noexcept(std::declval<const layout::InsetBus&>().revision(
                  std::declval<layout::InsetType>())),
              "revision must be noexcept");
static_assert(noexcept(std::declval<layout::InsetBus&>().setInset(
                  std::declval<layout::InsetType>(),
                  std::declval<const layout::EdgeInsets&>())),
              "setInset must be noexcept");
static_assert(
    sizeof(layout::EdgeInsets) == 4 * sizeof(float),
    "EdgeInsets must carry exactly four per-edge floats");
static_assert(
    static_cast<int>(layout::InsetType::SystemBars) == 0 &&
        static_cast<int>(layout::InsetType::DisplayCutout) == 1 &&
        static_cast<int>(layout::InsetType::Ime) == 2 &&
        static_cast<int>(layout::InsetType::TappableElement) == 3 &&
        static_cast<int>(layout::InsetType::SystemGestures) == 4 &&
        static_cast<int>(layout::InsetType::MandatorySystemGestures) == 5 &&
        static_cast<int>(layout::InsetType::CaptionBar) == 6 &&
        static_cast<int>(layout::InsetType::NavigationBars) == 7,
    "InsetType enumerators must cover all eight WindowInsets types in order");

// Named case: a fresh bus reads zero insets and revision 0 for every type.
static bool check_fresh_bus_zero_insets_revision_zero() {
  layout::InsetBus bus;
  for (const auto t : kAllTypes) {
    if (!insetsZero(bus.queryInset(t))) return false;
    if (bus.revision(t) != 0u) return false;
  }
  return true;
}

// Named case (TC-LAYOUT-032 core): each type is independently queryable per
// edge — distinct per-edge values round-trip exactly for every type.
static bool check_each_type_independently_queryable_per_edge() {
  layout::InsetBus bus;
  for (std::size_t i = 0; i < 8; ++i) {
    const float b = static_cast<float>(i);
    const layout::EdgeInsets e{1.0f + b, 2.0f + b, 3.0f + b, 4.0f + b};
    bus.setInset(kAllTypes[i], e);
    if (!insetsNear(bus.queryInset(kAllTypes[i]), e)) return false;
  }
  // All eight retain their own values simultaneously.
  for (std::size_t i = 0; i < 8; ++i) {
    const float b = static_cast<float>(i);
    const layout::EdgeInsets e{1.0f + b, 2.0f + b, 3.0f + b, 4.0f + b};
    if (!insetsNear(bus.queryInset(kAllTypes[i]), e)) return false;
  }
  return true;
}

// Named case (no cross-type leakage): writing one type leaves every other
// type's insets at zero and every other revision at 0.
static bool check_no_cross_type_leakage_on_write() {
  layout::InsetBus bus;
  const layout::EdgeInsets bars{10.0f, 20.0f, 30.0f, 40.0f};
  bus.setInset(layout::InsetType::SystemBars, bars);
  if (!insetsNear(bus.queryInset(layout::InsetType::SystemBars), bars)) {
    return false;
  }
  for (const auto t : kAllTypes) {
    if (t == layout::InsetType::SystemBars) continue;
    if (!insetsZero(bus.queryInset(t))) return false;
    if (bus.revision(t) != 0u) return false;
  }
  // A second write to a different type leaves the first type untouched.
  const layout::EdgeInsets ime{0.0f, 0.0f, 0.0f, 120.0f};
  bus.setInset(layout::InsetType::Ime, ime);
  if (!insetsNear(bus.queryInset(layout::InsetType::SystemBars), bars)) {
    return false;
  }
  if (!insetsNear(bus.queryInset(layout::InsetType::Ime), ime)) return false;
  if (!insetsZero(bus.queryInset(layout::InsetType::NavigationBars))) {
    return false;
  }
  return true;
}

// Named case (subscribable signal): each setInset bumps only that type's
// revision by exactly 1; polling observes every update.
static bool check_revision_bumps_only_for_touched_type() {
  layout::InsetBus bus;
  bus.setInset(layout::InsetType::SystemBars, layout::EdgeInsets{1.0f, 0.0f, 0.0f, 0.0f});
  if (bus.revision(layout::InsetType::SystemBars) != 1u) return false;
  bus.setInset(layout::InsetType::SystemBars, layout::EdgeInsets{2.0f, 0.0f, 0.0f, 0.0f});
  if (bus.revision(layout::InsetType::SystemBars) != 2u) return false;
  for (const auto t : kAllTypes) {
    if (t == layout::InsetType::SystemBars) continue;
    if (bus.revision(t) != 0u) return false;
  }
  return true;
}

// Named case: revisions are monotonic over a long scripted feed sequence.
static bool check_revision_monotonic_over_long_sequence() {
  layout::InsetBus bus;
  for (int i = 0; i < 50; ++i) {
    const auto t = kAllTypes[static_cast<std::size_t>(i) % 8];
    const std::uint64_t prev = bus.revision(t);
    const float f = static_cast<float>(i);
    bus.setInset(t, layout::EdgeInsets{f, f, f, f});
    if (bus.revision(t) != prev + 1u) return false;
  }
  return bus.revision(layout::InsetType::SystemBars) == 7u;
}

// Named case (Interface error shape): an unknown/out-of-range type reads
// zero insets and never throws on either query.
static bool check_unknown_type_reads_zero_and_never_throws() {
  layout::InsetBus bus;
  bus.setInset(layout::InsetType::SystemBars, layout::EdgeInsets{5.0f, 6.0f, 7.0f, 8.0f});
  bool threw = false;
  bool ok = false;
  try {
    const auto unknown = static_cast<layout::InsetType>(255);
    ok = insetsZero(bus.queryInset(unknown));
    (void)bus.revision(unknown);
    const auto negative = static_cast<layout::InsetType>(-1);
    ok = ok && insetsZero(bus.queryInset(negative));
    (void)bus.revision(negative);
  } catch (...) {
    threw = true;
  }
  return !threw && ok;
}

// Named case: last-write-wins within a single type.
static bool check_last_write_wins_within_type() {
  layout::InsetBus bus;
  bus.setInset(layout::InsetType::NavigationBars, layout::EdgeInsets{1.0f, 1.0f, 1.0f, 1.0f});
  const layout::EdgeInsets latest{9.0f, 8.0f, 7.0f, 6.0f};
  bus.setInset(layout::InsetType::NavigationBars, latest);
  return insetsNear(bus.queryInset(layout::InsetType::NavigationBars), latest);
}

// Named case: extreme/malformed feed values never throw and stay queryable.
static bool check_extreme_values_never_throw_and_stay_queryable() {
  layout::InsetBus bus;
  bool threw = false;
  try {
    bus.setInset(layout::InsetType::DisplayCutout,
                 layout::EdgeInsets{-1.0e9f, 1.0e9f, -0.0f, 1.0e30f});
    (void)bus.queryInset(layout::InsetType::DisplayCutout);
    (void)bus.revision(layout::InsetType::DisplayCutout);
    bus.setInset(layout::InsetType::Ime, kZero);
    (void)bus.queryInset(layout::InsetType::Ime);
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  if (bus.revision(layout::InsetType::DisplayCutout) != 1u) return false;
  return insetsZero(bus.queryInset(layout::InsetType::Ime));
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
  RUN_CASE(check_fresh_bus_zero_insets_revision_zero);
  RUN_CASE(check_each_type_independently_queryable_per_edge);
  RUN_CASE(check_no_cross_type_leakage_on_write);
  RUN_CASE(check_revision_bumps_only_for_touched_type);
  RUN_CASE(check_revision_monotonic_over_long_sequence);
  RUN_CASE(check_unknown_type_reads_zero_and_never_throws);
  RUN_CASE(check_last_write_wins_within_type);
  RUN_CASE(check_extreme_values_never_throw_and_stay_queryable);
  if (failures > 0) {
    std::printf("inset_signal_test: %d failing\n", failures);
    return 1;
  }
  std::printf("inset_signal_test: all passing\n");
  return 0;
}
