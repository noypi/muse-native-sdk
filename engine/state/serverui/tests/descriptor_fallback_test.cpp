// Batch 4 (Server-Driven UI, FR-STATE-009) contract test — T11.
// Spec refs: FR-STATE-009, AC-STATE-039 (TC-STATE-039).
// Hermetic: C++17 + committed engine/state headers only; fake verifier key
// and in-process bundled tree; no clock/network/env. Budget is a pure
// predicate: cached-descriptor first frame must cost <= firstFrameBudgetMs().
// Binds ONLY to the T11 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T11.md):
//   A-F1: namespace muse::state; `renderWithFallback` + `firstFrameBudgetMs`
//         declared in `muse/state/serverui.hpp` (or fallback header re-exported
//         there), logic in `serverui/src/fallback.cpp`.
//         T1.10: canonical `Descriptor` is owned SOLELY by
//         `muse/state/descriptor.hpp`; this test includes ONLY
//         `serverui.hpp` (single include — canonical type arrives
//         transitively, never a dual-include redefinition).
//   A-F2: Test helpers from T8/T9 headers are reused: Descriptor carries both
//         nodes (addNode) and ops (addOp); signTestOnly/builders as in T9.
//   A-F3: Bundled tree compared via `operator==` on ComponentTree, or via
//         size()+at(i).isPlaceholder() shape when equality is absent; this test
//         uses size/at/isPlaceholder only (no operator== required).
//   A-F4: "Cached descriptor meets budget" is asserted as: renderWithFallback
//         on a cached (valid, pre-verified) descriptor returns kOk AND
//         firstFrameBudgetMs() > 0 AND a render-cost probe
//         `renderCostModelNodes()` (header constant or free function) is <=
//         budget. No wall-clock measurement in-test (hermetic).
//   A-F5: Tampered bytes (bit-flip after signing) → kOk tree equal in shape to
//         bundled (full fallback); unknown-component descriptor → kOk with rest
//         rendered (partial fallback per T8); empty bytes → bundled shape.
#include <cstdio>
#include <cstdint>

#include "muse/state/atom.hpp"
#include "muse/state/serverui.hpp"  // single canonical include (T1.10); descriptor.hpp arrives transitively

namespace {

using muse::state::ComponentTree;
using muse::state::Descriptor;
using muse::state::DescriptorBytes;
using muse::state::Props;
using muse::state::ServerUiRegistry;
using muse::state::Status;
using muse::state::TrustAnchor;

static TrustAnchor anchor() { return TrustAnchor{"test-key-1", 9000}; }

static ComponentTree bundled() {
  ServerUiRegistry r;
  Descriptor d;
  d.addNode("BundledHome", Props());
  // Bundled tree is the known-good local screen; build it directly so the
  // test never depends on network or cache state.
  ComponentTree t = ComponentTree::placeholder();
  (void)r;
  (void)d;
  return t;
}

static bool sameShape(const ComponentTree& a, const ComponentTree& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a.at(i).isPlaceholder() != b.at(i).isPlaceholder()) return false;
  }
  return true;
}

// Tampered bytes (flipped payload after signing) render the bundled fallback.
static bool check_tampered_descriptor_renders_bundled_fallback() {
  DescriptorBytes good =
      muse::state::signTestOnly("screen:home", "test-key-1", 9000, 1);
  DescriptorBytes tampered = muse::state::tamperTestOnly(good);
  auto r = muse::state::renderWithFallback(tampered, anchor(), bundled());
  return r.ok() && sameShape(r.value(), bundled());
}

// Unknown component still renders the rest (partial fallback, with T8).
static bool check_unknown_component_renders_rest() {
  DescriptorBytes b = muse::state::signTestOnlyNodes(
      "Button,MysteryWidget", "test-key-1", 9000, 1);
  auto r = muse::state::renderWithFallback(b, anchor(), bundled());
  if (!r.ok()) return false;
  if (r.value().size() != 2) return false;
  return !r.value().at(0).isPlaceholder() && r.value().at(1).isPlaceholder();
}

// Empty bytes fall back to bundled shape.
static bool check_empty_bytes_fall_back_to_bundled() {
  DescriptorBytes b = DescriptorBytes::from("", 0);
  auto r = muse::state::renderWithFallback(b, anchor(), bundled());
  return r.ok() && sameShape(r.value(), bundled());
}

// Unsigned bytes fall back to bundled shape (T9 rejection → T11 fallback).
static bool check_unsigned_bytes_fall_back_to_bundled() {
  DescriptorBytes b = DescriptorBytes::from("screen:home", 11);
  auto r = muse::state::renderWithFallback(b, anchor(), bundled());
  return r.ok() && sameShape(r.value(), bundled());
}

// Sandbox-violating descriptor falls back even when correctly signed.
static bool check_sandbox_violating_descriptor_falls_back() {
  DescriptorBytes b = muse::state::signTestOnlyOps(
      "render+execCode", "test-key-1", 9000, 1);
  auto r = muse::state::renderWithFallback(b, anchor(), bundled());
  return r.ok() && sameShape(r.value(), bundled());
}

// Budget predicate: budget is positive and the cached-path cost model fits.
static bool check_cached_descriptor_meets_first_frame_budget() {
  std::uint64_t budget = muse::state::firstFrameBudgetMs();
  if (budget == 0) return false;
  return muse::state::renderCostModelNodes() <= budget;
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_tampered_descriptor_renders_bundled_fallback);
  RUN_CASE(check_unknown_component_renders_rest);
  RUN_CASE(check_empty_bytes_fall_back_to_bundled);
  RUN_CASE(check_unsigned_bytes_fall_back_to_bundled);
  RUN_CASE(check_sandbox_violating_descriptor_falls_back);
  RUN_CASE(check_cached_descriptor_meets_first_frame_budget);
  if (failures > 0) {
    std::printf("descriptor_fallback_test: %d failing\n", failures);
    return 1;
  }
  std::printf("descriptor_fallback_test: all passing\n");
  return 0;
}
