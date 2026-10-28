// T1 contract tests — FR-INPUT-001 / AC-INPUT-003 (TC-INPUT-003).
// Hermetic: C++17 + input headers only; no network, no env/config reads,
// no filesystem, no live services, no Android NDK (source classes are faked
// in-process via the plain NormalizeMotion parameters).
// Binds to the T1 `Interface:` line:
//   `NormalizedEvent NormalizeMotion(int32_t pointerId, ToolType tool,
//    uint32_t buttons, AxisState axes, uint32_t modifiers,
//    SourceClass sourceClass);`
// Include path follows the engine/runtime convention:
//   #include "input/event_model.h" with engine/input/include on the path.
//
// ASSUMPTION (enumerant names): the plan Detail names the ToolType members
// (touch/stylus/mouse/trackpad/key/gamepad/dpad) but neither the plan nor
// the spec settles the C++ enumerant spellings, the SourceClass enumerants,
// the AxisState fields, or the namespace. The names below
// (input::ToolType::{Touch,...}, input::SourceClass::{Touch,...},
// AxisState::{x,y,pressure}, NormalizedEvent fields) are this test's
// proposal. Reconciliation rule: if the coder names them differently,
// update these call sites — not the expected behavior (field preservation
// per sample, one normalized shape per source class).
//
// ASSUMPTION (SourceClass members): seven classes mirroring the seven tool
// types (Touch, Stylus, Mouse, Trackpad, Keyboard, Gamepad, Dpad), one
// sample injected per class per TC-INPUT-003. The spec's device list
// (FR-INPUT-007 context) supports this reading; rename-safe per above.
#include <cstdint>
#include <cstdio>

#include "input/event_model.h"

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

bool same_axes(const input::AxisState& a, const input::AxisState& b) {
  return a.x == b.x && a.y == b.y && a.pressure == b.pressure;
}

bool same_event(const input::NormalizedEvent& got, std::int32_t pointerId,
                input::ToolType tool, std::uint32_t buttons,
                const input::AxisState& axes, std::uint32_t modifiers,
                input::SourceClass sourceClass) {
  return got.pointerId == pointerId && got.tool == tool &&
         got.buttons == buttons && same_axes(got.axes, axes) &&
         got.modifiers == modifiers && got.sourceClass == sourceClass;
}

input::AxisState make_axes(float x, float y, float pressure) {
  input::AxisState axes{};
  axes.x = x;
  axes.y = y;
  axes.pressure = pressure;
  return axes;
}

// TC-INPUT-003: one sample per source class, identical normalized shape.
void case_touch_preserves_all_fields() {
  input::AxisState axes = make_axes(10.0f, 20.0f, 1.0f);
  input::NormalizedEvent got = input::NormalizeMotion(
      7, input::ToolType::Touch, 0u, axes, 0u, input::SourceClass::Touch);
  check(same_event(got, 7, input::ToolType::Touch, 0u, axes, 0u,
                   input::SourceClass::Touch),
        "touch_preserves_all_fields");
}

void case_stylus_preserves_all_fields() {
  input::AxisState axes = make_axes(30.0f, 40.0f, 0.5f);
  input::NormalizedEvent got = input::NormalizeMotion(
      3, input::ToolType::Stylus, 2u, axes, 1u, input::SourceClass::Stylus);
  check(same_event(got, 3, input::ToolType::Stylus, 2u, axes, 1u,
                   input::SourceClass::Stylus),
        "stylus_preserves_all_fields");
}

void case_mouse_preserves_buttons_and_axes() {
  input::AxisState axes = make_axes(100.0f, 200.0f, 0.0f);
  // Button mask bit 0 = primary, bit 1 = secondary (test-local convention).
  input::NormalizedEvent got = input::NormalizeMotion(
      1, input::ToolType::Mouse, 0x3u, axes, 0u, input::SourceClass::Mouse);
  check(same_event(got, 1, input::ToolType::Mouse, 0x3u, axes, 0u,
                   input::SourceClass::Mouse),
        "mouse_preserves_buttons_and_axes");
}

void case_trackpad_preserves_all_fields() {
  input::AxisState axes = make_axes(-5.0f, 12.5f, 0.0f);
  input::NormalizedEvent got = input::NormalizeMotion(
      2, input::ToolType::Trackpad, 0u, axes, 0u,
      input::SourceClass::Trackpad);
  check(same_event(got, 2, input::ToolType::Trackpad, 0u, axes, 0u,
                   input::SourceClass::Trackpad),
        "trackpad_preserves_all_fields");
}

void case_keyboard_preserves_modifiers() {
  input::AxisState axes = make_axes(0.0f, 0.0f, 0.0f);
  // Modifier mask bit 0 = shift (test-local convention).
  input::NormalizedEvent got = input::NormalizeMotion(
      0, input::ToolType::Key, 0u, axes, 0x1u, input::SourceClass::Keyboard);
  check(same_event(got, 0, input::ToolType::Key, 0u, axes, 0x1u,
                   input::SourceClass::Keyboard),
        "keyboard_preserves_modifiers");
}

void case_gamepad_preserves_axes() {
  input::AxisState axes = make_axes(-1.0f, 1.0f, 0.0f);
  input::NormalizedEvent got = input::NormalizeMotion(
      4, input::ToolType::Gamepad, 0x10u, axes, 0u,
      input::SourceClass::Gamepad);
  check(same_event(got, 4, input::ToolType::Gamepad, 0x10u, axes, 0u,
                   input::SourceClass::Gamepad),
        "gamepad_preserves_axes");
}

void case_dpad_preserves_all_fields() {
  input::AxisState axes = make_axes(0.0f, -1.0f, 0.0f);
  input::NormalizedEvent got = input::NormalizeMotion(
      5, input::ToolType::Dpad, 0u, axes, 0u, input::SourceClass::Dpad);
  check(same_event(got, 5, input::ToolType::Dpad, 0u, axes, 0u,
                   input::SourceClass::Dpad),
        "dpad_preserves_all_fields");
}

// Every source class maps to exactly one distinct normalized sourceClass.
void case_source_classes_are_distinct() {
  input::AxisState axes = make_axes(1.0f, 1.0f, 0.0f);
  input::SourceClass got[7];
  got[0] = input::NormalizeMotion(0, input::ToolType::Touch, 0u, axes, 0u,
                                  input::SourceClass::Touch)
               .sourceClass;
  got[1] = input::NormalizeMotion(0, input::ToolType::Stylus, 0u, axes, 0u,
                                  input::SourceClass::Stylus)
               .sourceClass;
  got[2] = input::NormalizeMotion(0, input::ToolType::Mouse, 0u, axes, 0u,
                                  input::SourceClass::Mouse)
               .sourceClass;
  got[3] = input::NormalizeMotion(0, input::ToolType::Trackpad, 0u, axes, 0u,
                                  input::SourceClass::Trackpad)
               .sourceClass;
  got[4] = input::NormalizeMotion(0, input::ToolType::Key, 0u, axes, 0u,
                                  input::SourceClass::Keyboard)
               .sourceClass;
  got[5] = input::NormalizeMotion(0, input::ToolType::Gamepad, 0u, axes, 0u,
                                  input::SourceClass::Gamepad)
               .sourceClass;
  got[6] = input::NormalizeMotion(0, input::ToolType::Dpad, 0u, axes, 0u,
                                  input::SourceClass::Dpad)
               .sourceClass;
  bool distinct = true;
  for (int i = 0; i < 7; ++i) {
    for (int j = i + 1; j < 7; ++j) {
      if (got[i] == got[j]) distinct = false;
    }
  }
  check(distinct, "source_classes_are_distinct");
}

// Normalization is a pure function: same input twice, identical output.
void case_normalize_is_deterministic() {
  input::AxisState axes = make_axes(3.25f, -7.5f, 0.75f);
  input::NormalizedEvent first = input::NormalizeMotion(
      9, input::ToolType::Mouse, 0x5u, axes, 0x2u, input::SourceClass::Mouse);
  input::NormalizedEvent second = input::NormalizeMotion(
      9, input::ToolType::Mouse, 0x5u, axes, 0x2u, input::SourceClass::Mouse);
  check(same_event(second, first.pointerId, first.tool, first.buttons,
                   first.axes, first.modifiers, first.sourceClass),
        "normalize_is_deterministic");
}

// Boundary: normalize never throws for any covered source class.
// NOTE (plan-interface gap): the T1 `Interface:` line names
// `normalize(RawSourceEvent)` returning `Status::kUnknownSource` for an
// unknown source class. The read-only production header exposes
// `NormalizeMotion(...)` returning `NormalizedEvent` by value with no
// `Status`/`RawSourceEvent` symbols, so an unknown-class Status case is
// not expressible against this header (see contract T1.md Conflicts).
// This case pins the "never throws" half of that requirement for every
// source class the header does define.
void case_normalize_never_throws() {
  input::AxisState axes = make_axes(1.0f, 2.0f, 0.5f);
  bool threw = false;
  try {
    (void)input::NormalizeMotion(0, input::ToolType::Touch, 0u, axes, 0u,
                                 input::SourceClass::Touch);
    (void)input::NormalizeMotion(0, input::ToolType::Stylus, 0u, axes, 0u,
                                 input::SourceClass::Stylus);
    (void)input::NormalizeMotion(0, input::ToolType::Mouse, 0u, axes, 0u,
                                 input::SourceClass::Mouse);
    (void)input::NormalizeMotion(0, input::ToolType::Trackpad, 0u, axes, 0u,
                                 input::SourceClass::Trackpad);
    (void)input::NormalizeMotion(0, input::ToolType::Key, 0u, axes, 0u,
                                 input::SourceClass::Keyboard);
    (void)input::NormalizeMotion(0, input::ToolType::Gamepad, 0u, axes, 0u,
                                 input::SourceClass::Gamepad);
    (void)input::NormalizeMotion(0, input::ToolType::Dpad, 0u, axes, 0u,
                                 input::SourceClass::Dpad);
  } catch (...) {
    threw = true;
  }
  check(!threw, "normalize_never_throws");
}

// Boundary: zero/empty sample and extreme values pass through exactly.
void case_boundary_zero_and_extreme_values() {
  input::AxisState zero = make_axes(0.0f, 0.0f, 0.0f);
  input::NormalizedEvent gotZero = input::NormalizeMotion(
      0, input::ToolType::Touch, 0u, zero, 0u, input::SourceClass::Touch);
  input::AxisState extreme = make_axes(100000.0f, -100000.0f, 1.0f);
  input::NormalizedEvent gotExtreme = input::NormalizeMotion(
      2147483647, input::ToolType::Stylus, 0xFFFFFFFFu, extreme, 0xFFFFFFFFu,
      input::SourceClass::Stylus);
  check(same_event(gotZero, 0, input::ToolType::Touch, 0u, zero, 0u,
                   input::SourceClass::Touch) &&
            same_event(gotExtreme, 2147483647, input::ToolType::Stylus,
                       0xFFFFFFFFu, extreme, 0xFFFFFFFFu,
                       input::SourceClass::Stylus),
        "boundary_zero_and_extreme_values");
}

}  // namespace

int main() {
  case_touch_preserves_all_fields();
  case_stylus_preserves_all_fields();
  case_mouse_preserves_buttons_and_axes();
  case_trackpad_preserves_all_fields();
  case_keyboard_preserves_modifiers();
  case_gamepad_preserves_axes();
  case_dpad_preserves_all_fields();
  case_source_classes_are_distinct();
  case_normalize_is_deterministic();
  case_normalize_never_throws();
  case_boundary_zero_and_extreme_values();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
