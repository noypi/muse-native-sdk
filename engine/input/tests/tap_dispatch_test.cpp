// TC-INPUT-022 (AC-INPUT-022) — tap-chain winner dispatches to the owning
// component event handle exactly once; unmatched or unmounted targets fire
// nothing (FR-INPUT-012). Batch 1 RED pin.
//
// Hermetic: C++17 + engine/input headers only; layout geometry is stubbed
// in-process (literal rects); no network, no env/config reads, no
// filesystem, no live services, no Android NDK.
// Include path follows the engine/input convention:
//   #include "gesture_arena.h" / #include "hit_test.h" with
//   engine/input/include on the path; NormalizedEvent comes from
//   #include "input/event_model.h".
//
// Batch-1 RED state: the winner-to-handle route signature is UNSETTLED —
// plan T6 is HALTED and routed to specs-creator ("ClaimTap/arena+slop
// resolves node id, JS dispatch shape unnamed ... do not invent"). This TU
// therefore invents no dispatch API. It drives the settled arena surface to
// prove the winner resolves, then fails closed on the missing dispatch
// half:
//
//   HAVE_DISPATCH == 0 today  ->  FAIL "no winner-to-handle dispatch route"
//   HAVE_DISPATCH == 1 (route named by specs-creator, implemented by coder,
//   registered as tap_dispatch_test in engine/input/CMakeLists.txt)
//   -> the TC-INPUT-022 cases below it run for real.
//
// The pending cases (compiled out until the route exists) are the normative
// AC-INPUT-022 pins the route must satisfy; they name behavior, not API:
//   (a) winner dispatches to the owning handle exactly once;
//   (b) a tap matching no node (unmatched) fires nothing;
//   (c) a tap whose winner node is unmounted fires nothing;
//   (d) replay of the same tap dispatches at most once in total.
// No fake dispatch is substituted: a passing fake while the Real is missing
// would be a green-on-divergent-Fake invalid suite (GR-12).
#include <cstdint>
#include <cstdio>

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

#if __has_include("input/tap_dispatch.h")
#include "input/tap_dispatch.h"
#define HAVE_DISPATCH 1
#else
#define HAVE_DISPATCH 0
#endif

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

input::NormalizedEvent make_touch(std::int32_t pointerId, float x, float y) {
  input::NormalizedEvent ev{};
  ev.pointerId = pointerId;
  ev.tool = input::ToolType::Touch;
  ev.sourceClass = input::SourceClass::Touch;
  ev.axes.x = x;
  ev.axes.y = y;
  ev.axes.pressure = 1.0f;
  return ev;
}

HitTestTree make_tree_with_button() {
  HitTestTree tree;
  tree.AddNode(/*id=*/1, /*parentId=*/0, HitRect{0.0f, 0.0f, 1080.0f, 2400.0f});
  tree.AddNode(/*id=*/21, /*parentId=*/1, HitRect{100.0f, 200.0f, 400.0f, 120.0f});
  return tree;
}

}  // namespace

int main() {
  // Arrange (settled surface): a tap inside the button rect resolves a winner.
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  const ArenaAction action =
      arena.AddPointer(make_touch(/*pointerId=*/7, /*x=*/150.0f, /*y=*/250.0f), tree);
  check(action.winnerId == 21, "arrange: tap inside button rect resolves winner node 21");

#if HAVE_DISPATCH
  // Normative TC-INPUT-022 pins. They compile only once specs-creator names
  // the route in input/tap_dispatch.h; until then they are documented intent
  // (see file header), not executed code. The owning agent for the route
  // signature is specs-creator (plan T6); the owning agent for the
  // implementation + ctest registration is the coder (plan T4--T6).
  //
  // (a) dispatch_to_handle(action.winnerId, ...) delivers exactly once;
  //     assert deliveries == 1.
  // (b) tap at (900, 2200) (no node) -> deliveries == 0.
  // (c) tap at a removed node id -> deliveries == 0.
  // (d) same tap dispatched twice -> deliveries == 1 in total.
  check(false, "TC-INPUT-022: route cases compile but have no assertions yet");
#else
  check(false,
        "TC-INPUT-022 RED: no winner-to-handle dispatch route "
        "(input/tap_dispatch.h missing; T6 HALTED pending specs-creator)");
#endif

  if (g_failures == 0) {
    std::printf("TC-INPUT-022 PASS\n");
    return 0;
  }
  std::printf("TC-INPUT-022 FAIL (%d failing)\n", g_failures);
  return 1;
}
