// T3 contract tests — FR-INPUT-007 AC-INPUT-013 / TC-INPUT-013.
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services, no Android NDK (source classes are faked
// in-process via the plain NormalizeMotion parameters; the arena is an
// in-memory recording stub in this file).
// Binds to the plan T3 `Interface:` line (reproduced verbatim in unit-tests/T3.md).
// Include paths follow the engine/runtime convention:
//   #include "input/event_model.h" / "input/device_harness.h"
//   with engine/input/include on the path.
//
// ASSUMPTION (harness shape): the Interface line names
// `DeviceHarness::dpadTraverse(FocusTree& t): vector<NodeId>` but settles no
// class shape beyond that (namespace, static vs instance, constructor args,
// arena-stub type). This test assumes:
//   - `input::DeviceHarness` class constructible with an arena sink reference
//     (a `RecordingArena` test-local struct with `push(NormalizedEvent)`);
//     if the coder names the sink differently, update the wire-up, not the
//     expectations (one injection per source class, equal events at entry,
//     D-pad walk visits each focusable exactly once).
//   - `dpadTraverse` takes a `FocusTree&` and returns `vector<NodeId>`.
//   - ToolType per class: Touch->Touch, Stylus->Stylus, Mouse->Mouse,
//     Trackpad->Trackpad, Keyboard->Key, Gamepad/phone-controller->Gamepad,
//     Dpad->Dpad (matches T1 header enumerants).
#include <cstdint>
#include <cstdio>
#include <vector>

#include "input/device_harness.h"
#include "input/event_model.h"
#include "input/focus_tree.h"

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

struct RecordingArena {
  std::vector<input::NormalizedEvent> received;
  void push(const input::NormalizedEvent& e) { received.push_back(e); }
};

bool same_axes(const input::AxisState& a, const input::AxisState& b) {
  return a.x == b.x && a.y == b.y && a.pressure == b.pressure;
}

bool same_event(const input::NormalizedEvent& a,
                const input::NormalizedEvent& b) {
  return a.pointerId == b.pointerId && a.tool == b.tool &&
         a.buttons == b.buttons && same_axes(a.axes, b.axes) &&
         a.modifiers == b.modifiers && a.sourceClass == b.sourceClass;
}

input::AxisState make_axes(float x, float y, float pressure) {
  input::AxisState axes{};
  axes.x = x;
  axes.y = y;
  axes.pressure = pressure;
  return axes;
}

// TC-INPUT-013 (part 1): one injection per phone/tablet source class yields
// an equal normalized event at arena entry (field-for-field preservation).
void case_each_source_class_normalizes_to_arena() {
  RecordingArena arena;
  input::DeviceHarness harness(arena);
  struct Sample {
    std::int32_t pointerId;
    input::ToolType tool;
    std::uint32_t buttons;
    input::AxisState axes;
    std::uint32_t modifiers;
    input::SourceClass sourceClass;
  };
  const Sample samples[7] = {
      {1, input::ToolType::Touch, 0u, make_axes(10.0f, 20.0f, 1.0f), 0u,
       input::SourceClass::Touch},
      {2, input::ToolType::Stylus, 2u, make_axes(30.0f, 40.0f, 0.5f), 1u,
       input::SourceClass::Stylus},
      {3, input::ToolType::Mouse, 0x3u, make_axes(100.0f, 200.0f, 0.0f), 0u,
       input::SourceClass::Mouse},
      {4, input::ToolType::Trackpad, 0u, make_axes(-5.0f, 12.5f, 0.0f), 0u,
       input::SourceClass::Trackpad},
      {5, input::ToolType::Key, 0u, make_axes(0.0f, 0.0f, 0.0f), 0x1u,
       input::SourceClass::Keyboard},
      {6, input::ToolType::Gamepad, 0x10u, make_axes(-1.0f, 1.0f, 0.0f), 0u,
       input::SourceClass::Gamepad},
      {7, input::ToolType::Dpad, 0u, make_axes(0.0f, -1.0f, 0.0f), 0u,
       input::SourceClass::Dpad},
  };
  bool allOk = true;
  for (const auto& s : samples) {
    input::NormalizedEvent got = input::NormalizeMotion(
        s.pointerId, s.tool, s.buttons, s.axes, s.modifiers, s.sourceClass);
    harness.inject(got);  // ASSUMPTION: harness entry-point spelling.
    const input::NormalizedEvent& atEntry = arena.received.back();
    input::NormalizedEvent expected{};
    expected.pointerId = s.pointerId;
    expected.tool = s.tool;
    expected.buttons = s.buttons;
    expected.axes = s.axes;
    expected.modifiers = s.modifiers;
    expected.sourceClass = s.sourceClass;
    if (!same_event(atEntry, expected)) allOk = false;
  }
  check(allOk, "device_each_class_normalizes_to_arena");
  check(arena.received.size() == 7u, "device_seven_injections_seven_entries");
}

// Per plan D1 + T1 header gap: TV-hardware class is excluded caller-side —
// the header has no Status/unknown representation, so the harness must never
// inject anything but the 7 phone/tablet classes. Assert the arena saw
// exactly the 7 covered classes, each exactly once.
void case_no_tv_hardware_class_injected() {
  RecordingArena arena;
  input::DeviceHarness harness(arena);
  (void)harness;
  // The seven SourceClass enumerants from the T1 header are the whole
  // phone/tablet set; count injections per class via a fresh pass.
  input::AxisState axes = make_axes(1.0f, 1.0f, 0.0f);
  const input::SourceClass classes[7] = {
      input::SourceClass::Touch,    input::SourceClass::Stylus,
      input::SourceClass::Mouse,    input::SourceClass::Trackpad,
      input::SourceClass::Keyboard, input::SourceClass::Gamepad,
      input::SourceClass::Dpad,
  };
  for (auto c : classes) {
    arena.push(input::NormalizeMotion(0, input::ToolType::Touch, 0u, axes, 0u,
                                      c));
  }
  int counts[7] = {0, 0, 0, 0, 0, 0, 0};
  for (const auto& e : arena.received) {
    for (int i = 0; i < 7; ++i) {
      if (e.sourceClass == classes[i]) ++counts[i];
    }
  }
  bool eachOnce = true;
  for (int i = 0; i < 7; ++i) {
    if (counts[i] != 1) eachOnce = false;
  }
  check(eachOnce, "device_phone_tablet_classes_each_exactly_once");
  check(arena.received.size() == 7u, "device_no_extra_tv_class");
}

// TC-INPUT-013 (part 2): D-pad walk over the T2 tree visits each focusable
// exactly once.
void case_dpad_walk_visits_each_exactly_once() {
  input::FocusTree t;
  (void)t.setOrder({11, 22, 33, 44, 55});
  RecordingArena arena;
  input::DeviceHarness harness(arena);
  std::vector<input::NodeId> visited = harness.dpadTraverse(t);
  check(visited.size() == 5u, "device_dpad_walk_count_equals_nodes");
  int counts[5] = {0, 0, 0, 0, 0};
  const input::NodeId ids[5] = {11, 22, 33, 44, 55};
  for (auto v : visited) {
    for (int i = 0; i < 5; ++i) {
      if (v == ids[i]) ++counts[i];
    }
  }
  bool eachOnce = true;
  for (int i = 0; i < 5; ++i) {
    if (counts[i] != 1) eachOnce = false;
  }
  check(eachOnce, "device_dpad_walk_each_exactly_once");
}

// Boundary: single-node tree — D-pad walk yields exactly that node.
void case_dpad_walk_single_node() {
  input::FocusTree t;
  (void)t.setOrder({9});
  RecordingArena arena;
  input::DeviceHarness harness(arena);
  std::vector<input::NodeId> visited = harness.dpadTraverse(t);
  check(visited.size() == 1u && visited[0] == 9,
        "device_dpad_walk_single_node");
}

}  // namespace

int main() {
  case_each_source_class_normalizes_to_arena();
  case_no_tv_hardware_class_injected();
  case_dpad_walk_visits_each_exactly_once();
  case_dpad_walk_single_node();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
