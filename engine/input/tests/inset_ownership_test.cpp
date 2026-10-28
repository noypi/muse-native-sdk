// T4 contract tests — FR-INPUT-006 / AC-INPUT-010 (TC-INPUT-010).
// Hermetic: C++17 + input headers only; the Inset Vector Bus is a test-double
// owned by this file (state/layout side stubbed, never implemented here); no
// network, no env/config reads, no filesystem, no live services.
// Binds to the T4 `Interface:` line (plan.md verbatim):
//   `void InsetConsumer::ApplyInsets(const TypedInsetsView& insets);`
//   (read-only view; returns void, owns nothing)
// Include path follows the engine/input convention:
//   #include "inset_consumer.h" with engine/input/include on the path.
//
// This decision only consumes the bus (plan A3): input holds no bus instance,
// registers no writer, and applies insets through the read-only view only.
//
// ASSUMPTION (TypedInsetsView shape): neither the plan nor the spec settles
// the C++ shape of the typed insets surface (owned by layout LAYOUT-INSETS,
// bus owned by state). This test proposes: namespace `input`; struct
// `TypedInsetsView` with plain `float contentW, contentH` (viewport size) plus
// `float systemBarTop, systemBarBottom, systemBarLeft, systemBarRight` (edge
// inset depths in the same units); all members public for const-only reads.
// Reconciliation rule: if the coder names fields differently, update these
// call sites — not the expected behavior (read-only consumption, rejection
// under system bars).
//
// ASSUMPTION (rejection query): ApplyInsets returns void, so rejection must be
// observed separately. This test proposes a const query
// `bool InsetConsumer::TouchBlocked(float x, float y) const` evaluated against
// the most recently applied view (stored as plain floats, never as a bus
// handle). Reconciliation: rename-safe per above.
//
// ASSUMPTION (edge rule): inset bounds are exclusive — a touch exactly on the
// inset edge (x == left, y == top, etc.) is allowed; only points strictly
// inside a system-bar region are blocked.
#include <cstdio>
#include <type_traits>
#include <vector>

#include "inset_consumer.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

// Test-double for the state-owned Inset Vector Bus. It hands out a read-only
// view via Current() and counts every Publish (writer registration/write).
// Input must never call Publish: after driving the consumer, writes == 0.
class FakeInsetBus {
 public:
  const input::TypedInsetsView& Current() const { return view_; }
  void Publish(const input::TypedInsetsView& view) {
    view_ = view;
    ++writes_;
  }
  int writes() const { return writes_; }

 private:
  input::TypedInsetsView view_{};
  int writes_ = 0;
};

input::TypedInsetsView make_view() {
  input::TypedInsetsView v{};
  v.contentW = 1080.0f;
  v.contentH = 2400.0f;
  v.systemBarTop = 48.0f;
  v.systemBarBottom = 120.0f;
  v.systemBarLeft = 0.0f;
  v.systemBarRight = 0.0f;
  return v;
}

// TC-INPUT-010 (read-only): the consumer compiles against a const view and a
// temporary — ApplyInsets takes `const TypedInsetsView&`, so this file proves
// at compile time that input needs no mutable access to the bus surface.
void ConsumesReadOnlyView(input::InsetConsumer& consumer,
                          const input::TypedInsetsView& view) {
  consumer.ApplyInsets(view);
}

void case_compiles_against_read_only_view() {
  input::InsetConsumer consumer;
  const input::TypedInsetsView view = make_view();
  ConsumesReadOnlyView(consumer, view);  // must compile: const& binding
  consumer.ApplyInsets(input::TypedInsetsView{});  // must compile: temporary
  check(true, "compiles_against_read_only_view");
}

// TC-INPUT-010 (zero writes): driving the consumer from the bus's Current()
// view performs no Publish / writer registration on the bus.
void case_zero_writes_to_bus() {
  FakeInsetBus bus;
  bus.Publish(make_view());  // test-side setup: 1 write by the owner, not input
  const int ownerWrites = bus.writes();
  input::InsetConsumer consumer;
  consumer.ApplyInsets(bus.Current());
  consumer.ApplyInsets(bus.Current());
  check(bus.writes() == ownerWrites, "zero_writes_to_bus");
}

// The view is plain data, not a bus handle: trivially copyable, so the
// consumer can retain values without retaining (or owning) the bus.
void case_view_is_plain_data() {
  check(std::is_trivially_copyable<input::TypedInsetsView>::value,
        "view_is_plain_data");
}

// TC-INPUT-010 (rejection): a touch under the top system bar is rejected.
void case_touch_under_top_bar_rejected() {
  input::InsetConsumer consumer;
  consumer.ApplyInsets(make_view());
  check(consumer.TouchBlocked(540.0f, 10.0f), "touch_under_top_bar_rejected");
}

// TC-INPUT-010 (rejection): a touch under the bottom system bar is rejected.
void case_touch_under_bottom_bar_rejected() {
  input::InsetConsumer consumer;
  consumer.ApplyInsets(make_view());
  check(consumer.TouchBlocked(540.0f, 2350.0f),
        "touch_under_bottom_bar_rejected");
}

// Touches under left/right bars are rejected when those insets are non-zero.
void case_touch_under_side_bars_rejected() {
  input::TypedInsetsView v = make_view();
  v.systemBarLeft = 24.0f;
  v.systemBarRight = 24.0f;
  input::InsetConsumer consumer;
  consumer.ApplyInsets(v);
  check(consumer.TouchBlocked(10.0f, 1200.0f) &&
            consumer.TouchBlocked(1070.0f, 1200.0f),
        "touch_under_side_bars_rejected");
}

// A touch in the content region (clear of every bar) is allowed.
void case_touch_in_content_allowed() {
  input::InsetConsumer consumer;
  consumer.ApplyInsets(make_view());
  check(!consumer.TouchBlocked(540.0f, 1200.0f), "touch_in_content_allowed");
}

// Boundary: a touch exactly on an inset edge is allowed (exclusive bounds).
void case_touch_on_inset_edge_allowed() {
  input::InsetConsumer consumer;
  consumer.ApplyInsets(make_view());
  check(!consumer.TouchBlocked(540.0f, 48.0f) &&
            !consumer.TouchBlocked(540.0f, 2400.0f - 120.0f),
        "touch_on_inset_edge_allowed");
}

// Boundary: zero insets block nothing — an edge-to-edge content touch passes.
void case_zero_insets_block_nothing() {
  input::TypedInsetsView v{};
  v.contentW = 1080.0f;
  v.contentH = 2400.0f;
  input::InsetConsumer consumer;
  consumer.ApplyInsets(v);
  check(!consumer.TouchBlocked(0.0f, 0.0f) &&
            !consumer.TouchBlocked(1079.0f, 2399.0f) &&
            !consumer.TouchBlocked(540.0f, 1200.0f),
        "zero_insets_block_nothing");
}

// Re-applying a view replaces the previous one (e.g. rotation changes the
// bars): the old rejection region no longer applies.
void case_reapply_replaces_previous_view() {
  input::InsetConsumer consumer;
  consumer.ApplyInsets(make_view());
  const bool blockedBefore = consumer.TouchBlocked(540.0f, 10.0f);
  input::TypedInsetsView v{};
  v.contentW = 1080.0f;
  v.contentH = 2400.0f;
  consumer.ApplyInsets(v);
  check(blockedBefore && !consumer.TouchBlocked(540.0f, 10.0f),
        "reapply_replaces_previous_view");
}

}  // namespace

int main() {
  case_compiles_against_read_only_view();
  case_zero_writes_to_bus();
  case_view_is_plain_data();
  case_touch_under_top_bar_rejected();
  case_touch_under_bottom_bar_rejected();
  case_touch_under_side_bars_rejected();
  case_touch_in_content_allowed();
  case_touch_on_inset_edge_allowed();
  case_zero_insets_block_nothing();
  case_reapply_replaces_previous_view();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
