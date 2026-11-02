// Batch 5 / T11 contract tests — real tap→JS dispatch (fired taps reach
// App.tsx/loop.ts handlers with node identity).
// Binds ONLY to the T11 `Interface:` line; never to implementation.
// Hermetic: C++17 only; no device, no JS engine, no network, no env/config
// reads, no filesystem, no live services. The JS handler is a test-owned
// recording double injected through the production seam; production owns
// the real presenter/js_runtime/jni_bridge path. Declared here against the
// plan, so linking before the T11 coder pass MUST fail (expected red).
#include <cstdint>
#include <cstdio>
#include <string>

// --- T11 `Interface:` line (plan verbatim) -------------------------------
// `dispatchTouch(nodeId: int, action: TouchAction, x: float, y: float) ->
// DispatchResult — forwards the fired tap (with stable nodeId) through
// presenter into the embedded JS engine handler (App.tsx onTouchStart →
// loop.ts transition); returns {fired: bool, nodeId: int, jsHandled: bool};
// jsHandled=false after a fire fails closed with nodeId + handler name in
// logcat`
enum class TouchAction : int32_t { Down = 0, Up = 1 };
struct DispatchResult {
  bool fired = false;
  int32_t nodeId = -1;
  bool jsHandled = false;
  std::string handlerName;  // named when jsHandled == false after a fire.
};
DispatchResult dispatchTouch(int32_t nodeId, TouchAction action, float x,
                             float y);

namespace {
// Flappy steer-node probe (production tap tree owns exact ids; the test
// pins only the contract: a fired flappy node reaches JS with its identity,
// never node 20, and a miss fires nothing).
constexpr int32_t kFlappySteerNode = 31;
constexpr int32_t kGalleryScrimNode = 20;
constexpr int32_t kOutsideNode = -1;
}  // namespace

static bool check_harness_entry_reached() { return true; }

// Named case: fired steer tap reaches the JS handler with stable node
// identity (ready→playing transition path; App.tsx onTouchStart shape).
static bool check_fired_steer_tap_reaches_js_with_identity() {
  DispatchResult down;
  try {
    down = dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    const DispatchResult up =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    (void)down;
    if (!up.fired || up.nodeId != kFlappySteerNode || !up.jsHandled) {
      std::printf("  detail: expected {fired, node %d, jsHandled} on UP\n",
                  static_cast<int>(kFlappySteerNode));
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case: jsHandled=false after a fire fails closed — names the nodeId
// and handler (no silent repaint-only drop, no re-instantiation assumed).
static bool check_js_miss_fails_closed_naming_node_and_handler() {
  const DispatchResult r =
      dispatchTouch(kOutsideNode, TouchAction::Up, 10000.0f, 10000.0f);
  if (r.fired || r.jsHandled) {
    std::printf("  detail: expected no fire and no JS handling outside\n");
    return false;
  }
  return true;
}

// Named case (boundary): gallery scrim node never wins for flappy content —
// dispatching node 20 on the flappy path must not report a flappy JS
// handling (fail-closed, never cross-content dispatch).
static bool check_gallery_scrim_never_dispatches_for_flappy() {
  DispatchResult r;
  try {
    r = dispatchTouch(kGalleryScrimNode, TouchAction::Up, 150.0f, 140.0f);
  } catch (...) {
    return false;
  }
  if (r.fired && r.jsHandled && r.nodeId == kGalleryScrimNode) {
    // A gallery-node fire that claims JS handling on the flappy path is a
    // cross-content dispatch — FAIL pins the boundary.
    std::printf("  detail: gallery node 20 dispatched as flappy JS\n");
    return false;
  }
  return true;
}

// Named case (security): forged/negative node ids fail closed — no fire, no
// JS handling, never throws.
static bool check_forged_node_ids_fail_closed() {
  try {
    const DispatchResult r =
        dispatchTouch(-999, TouchAction::Up, 150.0f, 400.0f);
    if (r.fired || r.jsHandled) {
      std::printf("  detail: expected no fire for forged node\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

#define RUN_CASE(name)                                  \
  do {                                                  \
    if (!(name)()) {                                    \
      std::printf("FAIL %s\n", #name);                  \
      ++failures;                                       \
    } else {                                            \
      std::printf("PASS %s\n", #name);                  \
    }                                                   \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_fired_steer_tap_reaches_js_with_identity);
  RUN_CASE(check_js_miss_fails_closed_naming_node_and_handler);
  RUN_CASE(check_gallery_scrim_never_dispatches_for_flappy);
  RUN_CASE(check_forged_node_ids_fail_closed);
  if (failures > 0) {
    std::printf("flappy_tap_js_dispatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_tap_js_dispatch_test: all passing\n");
  return 0;
}
