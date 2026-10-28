// T8 contract tests — FR-MOTION-007 / AC-MOTION-017 (TC-MOTION-017).
// Implicit flows: list reorder/insert/remove, adaptive compact<->expanded,
// theme color/elevation. Built on the T6 ImplicitRegistry.
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: explicit tMs per call,
// no wall clock, no randomness.
// Binds to the T8 `Interface:` line (reproduced verbatim in unit-tests/T8.md).
//
// ASSUMPTION (ItemMove shape): the plan names
// `ItemMove{PropertyId p; float fromIndex; float toIndex; bool removed}` in
// `motion::`. Field spellings below are the test's proposal; reconcile call
// sites, not expectations, if the coder names them differently.
//
// ASSUMPTION (opacity channel): the plan requires "removed items fade via
// opacity channel" but names no accessor. These tests use a proposed
// `float motion::ImplicitRegistry::getOpacity(PropertyId p, float tMs) const
// noexcept` returning 1.0 for live items and interpolating 1.0 -> 0.0 for
// removed items. If the coder exposes opacity differently (e.g. a reserved
// PropertyId channel or a struct return), update call sites, not expectations.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "motion/implicit.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

const float kValueEps = 1e-3f;

motion::PropertyId pid(std::uint32_t v) {
  motion::PropertyId p;
  p.value = v;
  return p;
}

motion::ItemMove moveItem(std::uint32_t id, float from, float to,
                          bool removed = false) {
  motion::ItemMove m;
  m.p = pid(id);
  m.fromIndex = from;
  m.toIndex = to;
  m.removed = removed;
  return m;
}

motion::TransitionSpec springSpec(float stiffness, float dampingRatio) {
  motion::TransitionSpec s;
  s.spring.stiffness = stiffness;
  s.spring.dampingRatio = dampingRatio;
  s.delayMs = 0.0f;
  s.staggerMs = 0.0f;
  return s;
}

// Reorder animates: immediately after the flow, get still reads the old
// position (no snap), then converges to the new index position.
bool check_reorder_animates_through_get() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(1), springSpec(180.0f, 1.0f));
  reg.set(pid(1), 0.0f, 0.0f);
  reg.applyLayoutFlow(std::vector<motion::ItemMove>{moveItem(1, 0.0f, 3.0f)},
                      0.0f);
  float atFlow = reg.get(pid(1), 0.0f);
  if (!near(atFlow, 0.0f, kValueEps)) return false;  // must not snap
  float settled = reg.get(pid(1), 5000.0f);
  return near(settled, 3.0f, 0.5f);
}

// Unknown PropertyId in moves is skipped: rest still applied, no throw.
bool check_unknown_property_id_is_skipped() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(1), springSpec(180.0f, 1.0f));
  reg.set(pid(1), 0.0f, 0.0f);
  bool threw = false;
  try {
    reg.applyLayoutFlow(
        std::vector<motion::ItemMove>{moveItem(1, 0.0f, 2.0f),
                                     moveItem(424242, 0.0f, 9.0f)},
        0.0f);
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  return near(reg.get(pid(1), 5000.0f), 2.0f, 0.5f);  // known item applied
}

// Removed items fade via the opacity channel: opacity leaves 1.0 and
// converges toward 0.0 instead of the item vanishing instantly.
bool check_removed_item_fades_via_opacity() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(2), springSpec(180.0f, 1.0f));
  reg.set(pid(2), 5.0f, 0.0f);
  reg.applyLayoutFlow(std::vector<motion::ItemMove>{moveItem(2, 5.0f, 5.0f,
                                                             true)},
                      0.0f);
  float opacityLate = reg.getOpacity(pid(2), 5000.0f);
  if (!(opacityLate < 1.0f)) return false;
  if (!(opacityLate >= 0.0f)) return false;
  return opacityLate < 0.05f;  // fully faded once settled
}

// Live (non-removed) items keep full opacity through a reorder flow.
bool check_live_item_keeps_full_opacity() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(3), springSpec(180.0f, 1.0f));
  reg.set(pid(3), 0.0f, 0.0f);
  reg.applyLayoutFlow(std::vector<motion::ItemMove>{moveItem(3, 0.0f, 1.0f)},
                      0.0f);
  return near(reg.getOpacity(pid(3), 5000.0f), 1.0f, 1e-3f);
}

// Theme toggle animates through get() interpolation (no snap): color and
// elevation move from the old toward the new values.
bool check_theme_toggle_animates() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(10), springSpec(180.0f, 1.0f));  // color channel
  reg.markImplicit(pid(11), springSpec(180.0f, 1.0f));  // elevation channel
  reg.set(pid(10), 0.0f, 0.0f);
  reg.set(pid(11), 0.0f, 0.0f);
  reg.applyTheme(/*colorFrom=*/0.0f, /*colorTo=*/1.0f, /*elevFrom=*/0.0f,
                 /*elevTo=*/8.0f, /*tMs=*/0.0f);
  float colorAt = reg.get(pid(10), 0.0f);
  float elevAt = reg.get(pid(11), 0.0f);
  if (!near(colorAt, 0.0f, kValueEps)) return false;  // no snap on color
  if (!near(elevAt, 0.0f, kValueEps)) return false;   // no snap on elevation
  return near(reg.get(pid(10), 5000.0f), 1.0f, 0.05f) &&
         near(reg.get(pid(11), 5000.0f), 8.0f, 0.5f);
}

// Empty flow is a no-op: no throw, existing values untouched.
bool check_empty_flow_is_noop() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(4), springSpec(180.0f, 1.0f));
  reg.set(pid(4), 7.0f, 0.0f);
  bool threw = false;
  try {
    reg.applyLayoutFlow(std::vector<motion::ItemMove>{}, 0.0f);
  } catch (...) {
    threw = true;
  }
  return !threw && near(reg.get(pid(4), 5000.0f), 7.0f, 0.5f);
}

// Degenerate routing (documented in implicit.h): a single-entry registry
// applies color to the lone entry and drops elevation — no second channel
// exists to move.
bool check_single_entry_theme_applies_color_drops_elevation() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(5), springSpec(180.0f, 1.0f));
  reg.set(pid(5), 0.0f, 0.0f);
  reg.applyTheme(/*colorFrom=*/0.0f, /*colorTo=*/1.0f, /*elevFrom=*/0.0f,
                 /*elevTo=*/8.0f, /*tMs=*/0.0f);
  if (!near(reg.get(pid(5), 0.0f), 0.0f, kValueEps)) return false;  // no snap
  return near(reg.get(pid(5), 5000.0f), 1.0f, 0.05f);  // color applied
}

// Degenerate routing (documented in implicit.h): with three entries, color
// targets the lowest id, elevation the highest, and the middle id is
// untouched.
bool check_three_entry_theme_leaves_middle_untouched() {
  motion::ImplicitRegistry reg;
  reg.markImplicit(pid(20), springSpec(180.0f, 1.0f));
  reg.markImplicit(pid(21), springSpec(180.0f, 1.0f));
  reg.markImplicit(pid(22), springSpec(180.0f, 1.0f));
  reg.set(pid(20), 0.0f, 0.0f);
  reg.set(pid(21), 42.0f, 0.0f);
  reg.set(pid(22), 0.0f, 0.0f);
  reg.applyTheme(/*colorFrom=*/0.0f, /*colorTo=*/1.0f, /*elevFrom=*/0.0f,
                 /*elevTo=*/8.0f, /*tMs=*/0.0f);
  if (!near(reg.get(pid(20), 5000.0f), 1.0f, 0.05f)) return false;  // color
  if (!near(reg.get(pid(22), 5000.0f), 8.0f, 0.5f)) return false;   // elevation
  return near(reg.get(pid(21), 5000.0f), 42.0f, kValueEps);  // middle intact
}

// Degenerate routing (documented in implicit.h): with zero entries,
// applyTheme and applyLayoutFlow are no-ops — no throw, unregistered reads
// stay 0.
bool check_zero_entry_theme_and_flow_are_noop() {
  motion::ImplicitRegistry reg;
  bool threw = false;
  try {
    reg.applyTheme(/*colorFrom=*/0.0f, /*colorTo=*/1.0f, /*elevFrom=*/0.0f,
                   /*elevTo=*/8.0f, /*tMs=*/0.0f);
    reg.applyLayoutFlow(std::vector<motion::ItemMove>{}, 0.0f);
    reg.applyLayoutFlow(
        std::vector<motion::ItemMove>{moveItem(9, 0.0f, 3.0f)}, 0.0f);
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  return near(reg.get(pid(9), 100.0f), 0.0f, kValueEps);
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_reorder_animates_through_get);
  RUN_CASE(check_unknown_property_id_is_skipped);
  RUN_CASE(check_removed_item_fades_via_opacity);
  RUN_CASE(check_live_item_keeps_full_opacity);
  RUN_CASE(check_theme_toggle_animates);
  RUN_CASE(check_empty_flow_is_noop);
  RUN_CASE(check_single_entry_theme_applies_color_drops_elevation);
  RUN_CASE(check_three_entry_theme_leaves_middle_untouched);
  RUN_CASE(check_zero_entry_theme_and_flow_are_noop);
  if (failures > 0) {
    std::printf("implicit_flow_test: %d failing\n", failures);
    return 1;
  }
  std::printf("implicit_flow_test: all passing\n");
  return 0;
}
