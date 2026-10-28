// T1 contract tests — FR-INPUT-003 / AC-INPUT-005 (TC-INPUT-005).
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services, no Android NDK (unbuffered dispatch is a
// process-local intent flag; palm thresholds are provisional per Q-INPUT-003).
// Binds to the T1 `Interface:` line (reproduced verbatim in T1.md):
//   `std::vector<input::NormalizedEvent> ExpandHistoricalSamples(
//    const input::NormalizedEvent& coalesced,
//    const std::vector<input::AxisState>& history);
//    bool IsPalmContact(const input::NormalizedEvent& ev, bool stylusActive);
//    void RequestUnbufferedDispatch(bool enable);
//    bool UnbufferedDispatchEnabled();`
// Include paths follow the engine/runtime convention:
//   #include "input/stylus_history.h" with engine/input/include on the path.
//
// ASSUMPTION A1: hermetic ctest suite, printf PASS/FAIL, nonzero on failure,
// same shape as engine/input/tests/event_model_test.cpp.
// ASSUMPTION A3: palm-rejection thresholds are provisional (Q-INPUT-003
// needs-evidence); this test encodes the heuristic direction only —
// large-contact/touch input while a stylus is active counts as palm, small
// stylus contacts do not. Device-lab tuning is deferred; if the coder picks
// different numeric cutoffs, update the boundary constants here — not the
// expected behavior (palm-during-stylus drops before the arena).

#include <cstdint>
#include <cstdio>
#include <vector>

#include "input/event_model.h"
#include "input/stylus_history.h"

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

input::NormalizedEvent make_stylus_event(float x, float y, float pressure,
                                         std::int32_t id) {
  input::NormalizedEvent ev{};
  ev.pointerId = id;
  ev.tool = input::ToolType::Stylus;
  ev.buttons = 0u;
  ev.axes.x = x;
  ev.axes.y = y;
  ev.axes.pressure = pressure;
  ev.modifiers = 0u;
  ev.sourceClass = input::SourceClass::Stylus;
  return ev;
}

input::NormalizedEvent make_touch_event(float x, float y, float pressure,
                                        std::int32_t id) {
  input::NormalizedEvent ev{};
  ev.pointerId = id;
  ev.tool = input::ToolType::Touch;
  ev.buttons = 0u;
  ev.axes.x = x;
  ev.axes.y = y;
  ev.axes.pressure = pressure;
  ev.modifiers = 0u;
  ev.sourceClass = input::SourceClass::Touch;
  return ev;
}

// AC-INPUT-005: every historical sample expands in order, coordinates kept.
void case_expand_preserves_count_order_coords() {
  // Synthetic 240Hz-style trace: 8 batched samples + 1 coalesced head.
  std::vector<input::AxisState> history;
  for (int i = 0; i < 8; ++i) {
    input::AxisState a{};
    a.x = 10.0f + static_cast<float>(i);
    a.y = 20.0f + 0.5f * static_cast<float>(i);
    a.pressure = 0.5f;
    history.push_back(a);
  }
  input::NormalizedEvent coalesced = make_stylus_event(18.0f, 24.0f, 0.5f, 1);
  std::vector<input::NormalizedEvent> out =
      input::ExpandHistoricalSamples(coalesced, history);
  bool ok = out.size() == history.size() + 1;
  for (std::size_t i = 0; ok && i < history.size(); ++i) {
    ok = out[i].axes.x == history[i].x && out[i].axes.y == history[i].y &&
         out[i].axes.pressure == history[i].pressure &&
         out[i].pointerId == coalesced.pointerId &&
         out[i].tool == input::ToolType::Stylus;
  }
  if (ok && !out.empty()) {
    const input::NormalizedEvent& head = out.back();
    ok = head.axes.x == coalesced.axes.x && head.axes.y == coalesced.axes.y;
  }
  check(ok, "expand_preserves_count_order_coords");
}

// Boundary: empty history expands to just the coalesced head.
void case_expand_empty_history_returns_head_only() {
  input::NormalizedEvent coalesced = make_stylus_event(5.0f, 6.0f, 0.4f, 2);
  std::vector<input::AxisState> history;
  std::vector<input::NormalizedEvent> out =
      input::ExpandHistoricalSamples(coalesced, history);
  check(out.size() == 1 && out[0].axes.x == 5.0f && out[0].axes.y == 6.0f,
        "expand_empty_history_returns_head_only");
}

// AC-INPUT-005 denial: palm contact during active stylus is flagged palm.
void case_palm_during_stylus_is_palm() {
  // Large-area touch contact (high pressure / broad touch) while the stylus
  // is down is the provisional palm signature (A3).
  input::NormalizedEvent palm = make_touch_event(100.0f, 200.0f, 1.0f, 9);
  check(input::IsPalmContact(palm, /*stylusActive=*/true),
        "palm_during_stylus_is_palm");
}

// Palm-flagged samples are dropped before arena dispatch: filtering the
// expanded trace on IsPalmContact leaves zero arena-bound events.
void case_palm_samples_dropped_before_arena() {
  input::NormalizedEvent palm = make_touch_event(100.0f, 200.0f, 1.0f, 9);
  std::vector<input::AxisState> history;
  input::AxisState a{};
  a.x = 100.0f;
  a.y = 200.0f;
  a.pressure = 1.0f;
  history.push_back(a);
  std::vector<input::NormalizedEvent> out =
      input::ExpandHistoricalSamples(palm, history);
  int arenaBound = 0;
  for (const auto& ev : out) {
    if (!input::IsPalmContact(ev, /*stylusActive=*/true)) ++arenaBound;
  }
  check(arenaBound == 0, "palm_samples_dropped_before_arena");
}

// Non-palm: genuine stylus tip contact is never flagged, stylus or not.
void case_stylus_tip_is_not_palm() {
  input::NormalizedEvent tip = make_stylus_event(11.0f, 22.0f, 0.5f, 1);
  check(!input::IsPalmContact(tip, /*stylusActive=*/true) &&
            !input::IsPalmContact(tip, /*stylusActive=*/false),
        "stylus_tip_is_not_palm");
}

// Touch contact with no active stylus is an ordinary finger, not palm.
void case_touch_without_stylus_is_not_palm() {
  input::NormalizedEvent finger = make_touch_event(50.0f, 60.0f, 0.4f, 4);
  check(!input::IsPalmContact(finger, /*stylusActive=*/false),
        "touch_without_stylus_is_not_palm");
}

// Unbuffered dispatch is a process-local intent flag with a default off.
void case_unbuffered_flag_enable_disable() {
  input::RequestUnbufferedDispatch(false);
  bool off = !input::UnbufferedDispatchEnabled();
  input::RequestUnbufferedDispatch(true);
  bool on = input::UnbufferedDispatchEnabled();
  input::RequestUnbufferedDispatch(false);
  bool offAgain = !input::UnbufferedDispatchEnabled();
  check(off && on && offAgain, "unbuffered_flag_enable_disable");
}

// Boundary: functions never throw (empty history, palm query, flag flip).
void case_never_throws() {
  bool threw = false;
  try {
    input::NormalizedEvent ev = make_stylus_event(1.0f, 2.0f, 0.5f, 1);
    std::vector<input::AxisState> empty;
    (void)input::ExpandHistoricalSamples(ev, empty);
    (void)input::IsPalmContact(ev, true);
    (void)input::IsPalmContact(ev, false);
    input::RequestUnbufferedDispatch(true);
    (void)input::UnbufferedDispatchEnabled();
    input::RequestUnbufferedDispatch(false);
  } catch (...) {
    threw = true;
  }
  check(!threw, "never_throws");
}

}  // namespace

int main() {
  case_expand_preserves_count_order_coords();
  case_expand_empty_history_returns_head_only();
  case_palm_during_stylus_is_palm();
  case_palm_samples_dropped_before_arena();
  case_stylus_tip_is_not_palm();
  case_touch_without_stylus_is_not_palm();
  case_unbuffered_flag_enable_disable();
  case_never_throws();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
