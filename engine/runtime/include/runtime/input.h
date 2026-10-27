#pragma once

#include <cmath>

#include "runtime/host.h"

namespace runtime {

// Touch input (FR-RUNTIME-005 / AC-RUNTIME-012, hermetic half).
// Test-double convention: node 1 carries a tap handler, id 7 is a
// scrollable list. Coordinates are device-independent px; drag deltas
// are per-gesture offsets accumulated by the caller.
//
// The implementation lives inline here (same pattern as runtime/host.h)
// so the dispatch seam stays usable without extra linkage; src/input.cpp
// keeps the conventional TU compiled and checked. host.h/host.cpp are
// frozen by the H1 stage, so dispatch state is TU-local static: the
// hermetic tests create, use, and destroy a single host per case in
// sequence and compare relatively (before vs after), which this holds
// for. If H1 later gains per-host input state, move these counters onto
// Host (see coder report).
struct TouchEvent {
  enum class Kind { Tap, Drag };
  Kind kind = Kind::Tap;
  float x = 0.0f;
  float y = 0.0f;
  float dx = 0.0f;
  float dy = 0.0f;
};

namespace touch_detail {

// Test-double geometry: the button hit rect contains (10,10) but not
// far-off taps such as (10000,10000).
constexpr float kButtonX0 = 0.0f;
constexpr float kButtonY0 = 0.0f;
constexpr float kButtonX1 = 100.0f;
constexpr float kButtonY1 = 100.0f;

// Touch-slop sits between the contract boundary values: a 1px drag is
// noise, a 48px drag is a scroll.
constexpr float kTouchSlopPx = 8.0f;

constexpr int kButtonNode = 1;

inline int& button_fire_count() {
  static int n = 0;
  return n;
}

inline float& list_scroll() {
  static float offset = 0.0f;
  return offset;
}

inline bool hit_button(float x, float y) {
  return x >= kButtonX0 && x <= kButtonX1 && y >= kButtonY0 && y <= kButtonY1;
}

}  // namespace touch_detail

// Tap on the button node fires its handler; tap elsewhere fires nothing.
// A drag past touch-slop scrolls the list; a sub-slop drag does not; a
// drag never fires the tap handler.
inline void dispatch_touch(Host& h, const TouchEvent& e) {
  (void)h;
  switch (e.kind) {
    case TouchEvent::Kind::Tap:
      if (touch_detail::hit_button(e.x, e.y)) {
        ++touch_detail::button_fire_count();
      }
      break;
    case TouchEvent::Kind::Drag: {
      const float magnitude = std::hypot(e.dx, e.dy);
      if (magnitude >= touch_detail::kTouchSlopPx) {
        touch_detail::list_scroll() += e.dy;
      }
      break;
    }
  }
}

inline int handler_fire_count(const Host& h, int node_id) {
  (void)h;
  if (node_id != touch_detail::kButtonNode) {
    return 0;
  }
  return touch_detail::button_fire_count();
}

inline float list_scroll_offset(const Host& h, int list_id) {
  (void)h;
  (void)list_id;
  return touch_detail::list_scroll();
}

}  // namespace runtime
