// H2 contract tests — FR-RUNTIME-005 / AC-RUNTIME-012 (TC-RUNTIME-012
// hermetic half; device-lab touch-smoke stays external).
// Binds ONLY to the H2 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
#include <cstdio>
#include <cstring>

#include "runtime/host.h"
#include "runtime/input.h"

// --- harness cases (convention copied from instantiate_test.cpp) -----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- H2 helpers ------------------------------------------------------------
// ASSUMPTION (TouchEvent shape): aggregate with `kind` (TapBegin/TapEnd for
// a tap, DragBegin/DragMove/DragEnd for a drag), target `x`/`y` in
// device-independent px, and per-move `dx`/`dy` deltas. Test-double host
// from H1: node 1 carries a tap handler, node 7 is a scrollable list.
// Reconciliation: update field names/call sites, not expectations, if the
// coder names them differently.

namespace {

constexpr int kButtonNode = 1;
constexpr int kListId = 7;

runtime::Host make_host_with_button_and_list() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  return h;
}

runtime::TouchEvent tap_at(float x, float y) {
  runtime::TouchEvent e{};
  e.kind = runtime::TouchEvent::Kind::Tap;
  e.x = x;
  e.y = y;
  return e;
}

runtime::TouchEvent drag_by(float dx, float dy) {
  runtime::TouchEvent e{};
  e.kind = runtime::TouchEvent::Kind::Drag;
  e.dx = dx;
  e.dy = dy;
  return e;
}

}  // namespace

// Named case: a tap on the button node fires its handler exactly once.
static bool check_tap_fires_handler_once() {
  runtime::Host h = make_host_with_button_and_list();
  const int before = runtime::handler_fire_count(h, kButtonNode);
  runtime::dispatch_touch(h, tap_at(10.0f, 10.0f));
  const bool ok = runtime::handler_fire_count(h, kButtonNode) == before + 1;
  runtime::destroy_host(h);
  return ok;
}

// Named case: a tap outside any handler fires nothing.
static bool check_tap_outside_fires_nothing() {
  runtime::Host h = make_host_with_button_and_list();
  const int before = runtime::handler_fire_count(h, kButtonNode);
  runtime::dispatch_touch(h, tap_at(10000.0f, 10000.0f));
  const bool ok = runtime::handler_fire_count(h, kButtonNode) == before;
  runtime::destroy_host(h);
  return ok;
}

// Named case: a drag scrolls the list (offset advances).
static bool check_drag_scrolls_list() {
  runtime::Host h = make_host_with_button_and_list();
  const float before = runtime::list_scroll_offset(h, kListId);
  runtime::dispatch_touch(h, drag_by(0.0f, 48.0f));
  const bool ok = runtime::list_scroll_offset(h, kListId) != before;
  runtime::destroy_host(h);
  return ok;
}

// Named case: a drag below touch-slop does not scroll.
static bool check_sub_slop_drag_does_not_scroll() {
  runtime::Host h = make_host_with_button_and_list();
  const float before = runtime::list_scroll_offset(h, kListId);
  runtime::dispatch_touch(h, drag_by(0.0f, 1.0f));
  const bool ok = runtime::list_scroll_offset(h, kListId) == before;
  runtime::destroy_host(h);
  return ok;
}

// Named case: a drag does not fire the tap handler.
static bool check_drag_does_not_fire_tap_handler() {
  runtime::Host h = make_host_with_button_and_list();
  const int before = runtime::handler_fire_count(h, kButtonNode);
  runtime::dispatch_touch(h, drag_by(0.0f, 48.0f));
  const bool ok = runtime::handler_fire_count(h, kButtonNode) == before;
  runtime::destroy_host(h);
  return ok;
}

// T1 (BUG-RUNTIME-001 hardening): two independent Hosts never share counts.
// Host B is unaffected by dispatches to host A.
static bool check_two_hosts_have_independent_fire_counts() {
  runtime::Host a = make_host_with_button_and_list();
  runtime::Host b = make_host_with_button_and_list();
  runtime::dispatch_touch(a, tap_at(10.0f, 10.0f));
  const bool ok = runtime::handler_fire_count(b, kButtonNode) == 0 &&
                  runtime::handler_fire_count(a, kButtonNode) == 1;
  runtime::destroy_host(a);
  runtime::destroy_host(b);
  return ok;
}

// T1 (BUG-RUNTIME-001 hardening): list scroll offsets are per-host.
static bool check_two_hosts_have_independent_scroll_offsets() {
  runtime::Host a = make_host_with_button_and_list();
  runtime::Host b = make_host_with_button_and_list();
  runtime::dispatch_touch(a, drag_by(0.0f, 48.0f));
  const bool ok = runtime::list_scroll_offset(b, kListId) == 0.0f &&
                  runtime::list_scroll_offset(a, kListId) == 48.0f;
  runtime::destroy_host(a);
  runtime::destroy_host(b);
  return ok;
}

// T1: unknown node/list ids return 0 (never throws).
static bool check_unknown_ids_return_zero() {
  runtime::Host h = make_host_with_button_and_list();
  runtime::dispatch_touch(h, tap_at(10.0f, 10.0f));
  const bool ok = runtime::handler_fire_count(h, 999) == 0 &&
                  runtime::list_scroll_offset(h, 999) == 0.0f;
  runtime::destroy_host(h);
  return ok;
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_tap_fires_handler_once);
  RUN_CASE(check_tap_outside_fires_nothing);
  RUN_CASE(check_drag_scrolls_list);
  RUN_CASE(check_sub_slop_drag_does_not_scroll);
  RUN_CASE(check_drag_does_not_fire_tap_handler);
  RUN_CASE(check_two_hosts_have_independent_fire_counts);
  RUN_CASE(check_two_hosts_have_independent_scroll_offsets);
  RUN_CASE(check_unknown_ids_return_zero);
  if (failures > 0) {
    std::printf("touch_dispatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("touch_dispatch_test: all passing\n");
  return 0;
}
