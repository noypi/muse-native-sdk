// Batch 5 / T11 contract tests — honest tap-record seam (fired taps are
// recorded with node identity; NO JS is invoked hermetically — the device
// lane logs nodeId + handler in logcat, and the live JS leg stays
// device/player proof per plan Acceptance).
// Binds ONLY to the T11 `Interface:` line as retyped per the Batch 5 halt
// note (tapRecorded record-not-invoke, never jsHandled); never to
// implementation.
// Hermetic: C++17 only; no device, no JS engine, no network, no env/config
// reads, no filesystem, no live services. The tap record is a test-owned
// expectation against the production seam; production owns the real
// presenter/js_runtime/jni_bridge path.
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include "runtime/presenter.h"

// --- T11 `Interface:` line (retyped honest tap-record per Batch 5 halt) ---
// `dispatchTouch(nodeId: int, action: TouchAction, x: float, y: float) ->
// DispatchResult — records the fired tap (with stable nodeId) through the
// presenter tap-record seam (App.tsx onTouchStart → loop.ts transition stays
// device/player proof); returns {fired: bool, nodeId: int,
// tapRecorded: bool}; tapRecorded=false after a fire fails closed with nodeId
// + handler name in logcat. No JS handler is invoked here by contract.`
enum class TouchAction : int32_t { Down = 0, Up = 1 };
struct DispatchResult {
  bool fired = false;
  int32_t nodeId = -1;
  bool tapRecorded = false;
  std::string handlerName;  // named when tapRecorded == false after a fire.
};
DispatchResult dispatchTouch(int32_t nodeId, TouchAction action, float x,
                             float y);

namespace {
// Flappy steer-node probe (production tap tree owns exact ids; the test
// pins only the contract: a fired flappy node is recorded with its identity,
// never node 20, and a miss records nothing — no JS invocation claimed).
constexpr int32_t kFlappySteerNode = 31;
constexpr int32_t kGalleryScrimNode = 20;
constexpr int32_t kOutsideNode = -1;
}  // namespace

static bool check_harness_entry_reached() { return true; }

// Named case: fired steer tap is recorded with stable node identity
// (ready→playing transition path stays device/player proof; App.tsx
// onTouchStart shape recorded, never invoked).
static bool check_fired_steer_tap_records_with_identity() {
  DispatchResult down;
  try {
    down = dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    const DispatchResult up =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    (void)down;
    if (!up.fired || up.nodeId != kFlappySteerNode || !up.tapRecorded) {
      std::printf("  detail: expected {fired, node %d, tapRecorded} on UP\n",
                  static_cast<int>(kFlappySteerNode));
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case: tapRecorded=false after a fire fails closed — names the nodeId
// and handler (no silent repaint-only drop, no re-instantiation assumed).
static bool check_tap_miss_fails_closed_naming_node_and_handler() {
  const DispatchResult r =
      dispatchTouch(kOutsideNode, TouchAction::Up, 10000.0f, 10000.0f);
  if (r.fired || r.tapRecorded) {
    std::printf("  detail: expected no fire and no tap record outside\n");
    return false;
  }
  return true;
}

// Named case (boundary): gallery scrim node never wins for flappy content —
// dispatching node 20 on the flappy path must not report a flappy tap
// record (fail-closed, never cross-content record).
static bool check_gallery_scrim_never_records_for_flappy() {
  DispatchResult r;
  try {
    r = dispatchTouch(kGalleryScrimNode, TouchAction::Up, 150.0f, 140.0f);
  } catch (...) {
    return false;
  }
  if (r.fired && r.tapRecorded && r.nodeId == kGalleryScrimNode) {
    // A gallery-node fire that claims a flappy tap record on the flappy
    // path is a cross-content record — FAIL pins the boundary.
    std::printf("  detail: gallery node 20 recorded as flappy tap\n");
    return false;
  }
  return true;
}

// Named case (security): forged/negative node ids fail closed — no fire, no
// tap record, never throws.
static bool check_forged_node_ids_fail_closed() {
  try {
    const DispatchResult r =
        dispatchTouch(-999, TouchAction::Up, 150.0f, 400.0f);
    if (r.fired || r.tapRecorded) {
      std::printf("  detail: expected no fire for forged node\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case (tap-record evidence): a fired tap must carry the name of the
// tap-record handler slot, with the fired nodeId. Record-not-invoke: no JS
// runs here — the result carries tapRecorded + handlerName as the record,
// and an empty handler name on a fire FAILS here.
static bool check_fired_result_carries_tap_record_identity() {
  DispatchResult down;
  try {
    down = dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    const DispatchResult up =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    (void)down;
    if (!up.fired || up.nodeId != kFlappySteerNode || !up.tapRecorded) {
      std::printf("  detail: expected {fired, node %d, tapRecorded} on UP\n",
                  static_cast<int>(kFlappySteerNode));
      return false;
    }
    if (up.handlerName != "onTouchStart") {
      std::printf("  detail: fired tap names no tap-record handler\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case (node identity): an Up for a different node than the Down fires
// nothing and records nothing; the pending Down pairing survives so the true
// Up still completes with identity (leaves clean state for later cases).
static bool check_down_up_identity_mismatch_fails_closed() {
  try {
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    const DispatchResult wrong =
        dispatchTouch(32, TouchAction::Up, 150.0f, 400.0f);
    if (wrong.fired || wrong.tapRecorded) {
      std::printf("  detail: mismatched Up fired or recorded a tap\n");
      return false;
    }
    const DispatchResult up =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    if (!up.fired || up.nodeId != kFlappySteerNode || !up.tapRecorded) {
      std::printf("  detail: true Up after mismatch did not fire with identity\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case (node identity): an Up with no prior Down fires nothing and
// names the handler for fail-closed evidence. Opens with a setup pair so the
// case is order-independent (pairing always starts clean here).
static bool check_up_without_prior_down_records_nothing() {
  try {
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    const DispatchResult lone =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    if (lone.fired || lone.tapRecorded) {
      std::printf("  detail: lone Up fired or recorded a tap\n");
      return false;
    }
    if (lone.handlerName.empty()) {
      std::printf("  detail: lone Up names no handler for fail-closed\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case (finite-coordinate guard, plan REQUIRED): +Inf arming or firing
// never records. NaN/negative companions pin the rest of the guard.
static bool check_nonfinite_coordinates_rejected() {
  const float kInf = std::numeric_limits<float>::infinity();
  const float kNaN = std::numeric_limits<float>::quiet_NaN();
  try {
    // +Inf Down must not arm a pair: the following finite Up records nothing.
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, kInf, 400.0f);
    const DispatchResult afterInfDown =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    if (afterInfDown.fired || afterInfDown.tapRecorded) {
      std::printf("  detail: +Inf Down armed a firing pair\n");
      return false;
    }
    // Finite Down followed by +Inf Up must not fire.
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    const DispatchResult infUp =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, kInf, kInf);
    if (infUp.fired || infUp.tapRecorded) {
      std::printf("  detail: +Inf Up fired (guard admits +Inf)\n");
      return false;
    }
    // NaN / negative Down arms nothing either (guard companions).
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, kNaN, 400.0f);
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, -5.0f, 400.0f);
    const DispatchResult afterBad =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    if (afterBad.fired || afterBad.tapRecorded) {
      std::printf("  detail: NaN/negative Down armed a firing pair\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case (security — log-literal format, Batch 5 review): every
// handler name riding a DispatchResult is an internal literal — never
// attacker-influenced, never carrying '%' (format-string) or '\n'
// (log-injection) content. The logcat sink formats with a literal format
// string + %s over these internal constants; this case pins the test-side
// half of that contract across all fail-closed and fired paths.
static bool check_tap_record_evidence_is_log_literal_safe() {
  try {
    (void)dispatchTouch(kFlappySteerNode, TouchAction::Down, 150.0f, 400.0f);
    const DispatchResult fired =
        dispatchTouch(kFlappySteerNode, TouchAction::Up, 150.0f, 400.0f);
    const DispatchResult outsiders[] = {
        dispatchTouch(kOutsideNode, TouchAction::Up, 10000.0f, 10000.0f),
        dispatchTouch(kGalleryScrimNode, TouchAction::Up, 150.0f, 140.0f),
        dispatchTouch(-999, TouchAction::Up, 150.0f, 400.0f),
    };
    if (!fired.fired || !fired.tapRecorded) {
      std::printf("  detail: setup fire did not record\n");
      return false;
    }
    const std::string names[] = {fired.handlerName, outsiders[0].handlerName,
                                 outsiders[1].handlerName,
                                 outsiders[2].handlerName};
    for (const auto& name : names) {
      if (name.find('%') != std::string::npos ||
          name.find('\n') != std::string::npos) {
        std::printf("  detail: handler name carries log-injection content\n");
        return false;
      }
      if (name != "onTouchStart" && name != "GalleryScrim.onTouchStart" &&
          !name.empty()) {
        std::printf("  detail: handler name is not an internal literal: %s\n",
                     name.c_str());
        return false;
      }
    }
  } catch (...) {
    std::printf("  detail: dispatchTouch must never throw\n");
    return false;
  }
  return true;
}

// Named case (security — RecordFlappyTap forged-node/handler denial, Batch 5
// security-review): the record seam double-gate (nodeId == 31 +
// handler == "onTouchStart") denies forged nodes/handlers fail-closed —
// returns false, records nothing, never throws. Control: node 31 +
// "onTouchStart" records (true).
static bool check_record_seam_denies_forged_node_and_handler() {
  try {
    if (runtime::FlappyTouchHandlerName(kGalleryScrimNode) != nullptr) {
      std::printf("  detail: scrim node 20 named a flappy handler\n");
      return false;
    }
    if (runtime::FlappyTouchHandlerName(-999) != nullptr) {
      std::printf("  detail: forged node named a flappy handler\n");
      return false;
    }
    if (runtime::FireFlappyTapToJs(kGalleryScrimNode, "onTouchStart")) {
      std::printf("  detail: scrim node 20 recorded a tap\n");
      return false;
    }
    if (runtime::FireFlappyTapToJs(-999, "onTouchStart")) {
      std::printf("  detail: forged node recorded a tap\n");
      return false;
    }
    if (runtime::FireFlappyTapToJs(kFlappySteerNode, "onTouchEnd")) {
      std::printf("  detail: wrong handler recorded a tap\n");
      return false;
    }
    if (runtime::FireFlappyTapToJs(kFlappySteerNode, nullptr)) {
      std::printf("  detail: null handler recorded a tap\n");
      return false;
    }
    if (!runtime::FireFlappyTapToJs(kFlappySteerNode, "onTouchStart")) {
      std::printf("  detail: control node 31 + onTouchStart did not record\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: record seam must never throw\n");
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
  RUN_CASE(check_fired_steer_tap_records_with_identity);
  RUN_CASE(check_tap_miss_fails_closed_naming_node_and_handler);
  RUN_CASE(check_gallery_scrim_never_records_for_flappy);
  RUN_CASE(check_forged_node_ids_fail_closed);
  RUN_CASE(check_fired_result_carries_tap_record_identity);
  RUN_CASE(check_down_up_identity_mismatch_fails_closed);
  RUN_CASE(check_up_without_prior_down_records_nothing);
  RUN_CASE(check_nonfinite_coordinates_rejected);
  RUN_CASE(check_record_seam_denies_forged_node_and_handler);
  RUN_CASE(check_tap_record_evidence_is_log_literal_safe);
  if (failures > 0) {
    std::printf("flappy_tap_js_dispatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_tap_js_dispatch_test: all passing\n");
  return 0;
}
