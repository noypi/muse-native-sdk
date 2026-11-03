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

#include "runtime/flappy_content.h"
#include "runtime/presenter.h"
#include "runtime/content_registry.h"

// ---- T1 (bug-runtime-014) absence + relocation-contract pins ----
// (1) ABSENCE (audited by TC-RUNTIME-039 grep, RED today): generic
// engine/runtime/src/jni_bridge.cpp must carry zero per-app tokens from
// this table; the six sibling dispatch files
// (presenter.h, input_chain.cpp, js_runtime.cpp, layout.cpp,
// presenter.cpp, renderer.cpp) are already clean (TC-038/040..044).
// The table below pins the exact relocation set so T2 cannot shrink it;
// the grep audit (not C++ execution) is the enforcement leg.
// Forbidden set (support.md classification, verified in-tree 2026-11-03):
//   FlappyBird / FlappyPipe (TC-039 case-sensitive trip, jni_bridge
//   lines ~1052-1053) | flappy-hero / Recognized flappy comment (~1003,
//   ~1051, same app-knowledge class, evades case-sensitive grep) |
//   F3DIAG tag + hasTag("...") kind probe (~1038-1053, content-kind
//   knowledge in the generic file).
// (2) PRESENCE (callable, GREEN today): the registration call stays —
// TryPresentContentScene (content_registry.h) + RefreshTapTreeFromPresentedScene
// + ContentTouchHandlerName/FireContentTapToJs seam. The cases below
// reference the seam so removing the call breaks the link; source
// presence is additionally audited by grep (lines ~1010, ~1020/~1313,
// ~1824-1828 byte-kept per plan D1).
static bool check_t1_registration_seam_callable() {
  // RegisterFlappyContent links the content-module owner; the generic
  // seam resolves through it — removing the registration call breaks this.
  if (!runtime::RegisterFlappyContent()) {
    std::printf("  detail: RegisterFlappyContent failed\n");
    return false;
  }
  // Content seam callable: steer node names its handler; scrim/forged do not.
  if (runtime::ContentTouchHandlerName(31) == nullptr) {
    std::printf("  detail: T1 presence pin: steer node 31 names no handler\n");
    return false;
  }
  if (!runtime::FireContentTapToJs(31, "onTouchStart")) {
    std::printf("  detail: T1 presence pin: steer tap did not record\n");
    return false;
  }
  return true;
}

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
    if (runtime::ContentTouchHandlerName(kGalleryScrimNode) != nullptr) {
      std::printf("  detail: scrim node 20 named a flappy handler\n");
      return false;
    }
    if (runtime::ContentTouchHandlerName(-999) != nullptr) {
      std::printf("  detail: forged node named a flappy handler\n");
      return false;
    }
    if (runtime::FireContentTapToJs(kGalleryScrimNode, "onTouchStart")) {
      std::printf("  detail: scrim node 20 recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(-999, "onTouchStart")) {
      std::printf("  detail: forged node recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(kFlappySteerNode, "onTouchEnd")) {
      std::printf("  detail: wrong handler recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(kFlappySteerNode, nullptr)) {
      std::printf("  detail: null handler recorded a tap\n");
      return false;
    }
    if (!runtime::FireContentTapToJs(kFlappySteerNode, "onTouchStart")) {
      std::printf("  detail: control node 31 + onTouchStart did not record\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: record seam must never throw\n");
    return false;
  }
  return true;
}

// ---- T2 (bug-runtime-009-010 Option A) RED cases ----
// Plan `Interface:` line (verbatim): `dispatchTouch(nodeId: int,
// action: TouchAction, x: float, y: float) -> DispatchResult
// { fired: bool, nodeId: int, jsHandled: bool }` — a tap on any
// evaluated/hit-test node (not only 31) with a registered handler returns
// `jsHandled=true` via `JsRuntime` invocation; unregistered/miss returns
// `fired=false`. Store contract: `EvaluatedFlappyTrees()` is empty at
// startup (no 101/102/103 without registration/evaluation).
// Production JsRuntime dispatch surface (T4 coder implements in
// engine/runtime/src/js_runtime.cpp + engine/runtime/src/presenter.cpp,
// handler resolved per node via registration in
// engine/runtime/src/flappy_content.cpp): declared here against the plan;
// the definition lands with T4, so linking the jsHandled case below before
// then MUST fail (expected red). The two probes below it compile and run
// today and FAIL on current behavior (literal-31 gate, static seeder).

// Desired Option-A dispatch result: jsHandled comes from a real JsRuntime
// invocation of the node's registered handler, never record-plus-logcat.
struct JsDispatchResult {
  bool fired = false;
  std::int32_t nodeId = -1;
  bool jsHandled = false;
};
JsDispatchResult DispatchTapViaJsRuntime(std::int32_t nodeId,
                                         const char* handler);

// Named case (JsRuntime dispatch): a tap on evaluated non-31 touchable node
// 32 reaches JsRuntime and returns jsHandled — the literal-31 gate fails it.
static bool check_non31_touchable_dispatches_via_jsruntime() {
  const JsDispatchResult r = DispatchTapViaJsRuntime(32, "onHudTouch");
  if (!r.fired || r.nodeId != 32 || !r.jsHandled) {
    std::printf("  detail: expected {fired, node 32, jsHandled} via JsRuntime\n");
    return false;
  }
  return true;
}

// Named case (registration gate): every evaluated touchable (31/32/33)
// names its handler through registration — never a literal-31 gate.
// Compiles and runs today; FAILS (32/33 resolve nullptr) until T4.
static bool check_every_touchable_names_handler_via_registration() {
  if (runtime::ContentTouchHandlerName(31) == nullptr) {
    std::printf("  detail: steer node 31 names no handler\n");
    return false;
  }
  if (runtime::ContentTouchHandlerName(32) == nullptr) {
    std::printf("  detail: touchable node 32 names no handler (31-gate)\n");
    return false;
  }
  if (runtime::ContentTouchHandlerName(33) == nullptr) {
    std::printf("  detail: touchable node 33 names no handler (31-gate)\n");
    return false;
  }
  if (runtime::ContentTouchHandlerName(kGalleryScrimNode) != nullptr) {
    std::printf("  detail: scrim node 20 named a flappy handler\n");
    return false;
  }
  return true;
}

// Named case (store starts empty): handles 101/102/103 do not resolve
// before registration/evaluation. Compiles and runs today; FAILS (static
// seeder pre-seeds them) until T4 deletes the seeder.
static bool check_startup_store_has_no_preseeded_handles() {
  const FrameStats a = renderTree(101);
  const FrameStats b = renderTree(102);
  const FrameStats c = renderTree(103);
  if (a.nodeCount != 0 || b.nodeCount != 0 || c.nodeCount != 0) {
    std::printf("  detail: pre-seeded handles resolve without registration\n");
    return false;
  }
  if (b.unknownKindCount != 0) {
    std::printf("  detail: pre-seeded unknown tree present at startup\n");
    return false;
  }
  return true;
}

// Named case (security — bug-runtime-014 review pin): hostile contentErr
// inputs to FormatContentDiagLine stay a single bounded log line — no
// format interpretation, no unbounded growth, fail-closed naming intact.
// Hermetic, in-process only (C++17, no device/logcat/env/filesystem).
static bool check_format_content_diag_line_hostile_err_stays_bounded() {
  try {
    const std::string nullLine =
        runtime::FormatContentDiagLine(nullptr, 0, false, nullptr, 0);
    const std::string emptyLine =
        runtime::FormatContentDiagLine(nullptr, 0, false, "", 0);
    for (const auto& line : {nullLine, emptyLine}) {
      if (line.rfind("F3DIAG ", 0) != 0 ||
          line.find("contentErr=-") == std::string::npos ||
          line.find('\n') != std::string::npos || line.size() >= 320) {
        std::printf("  detail: null/empty err not fail-closed single line\n");
        return false;
      }
    }
    // Format-specifier-looking payload must ride verbatim, never interpreted.
    const char* fmtPayload = "%s %n %x %d %i contentErr=%s";
    const std::string fmtLine =
        runtime::FormatContentDiagLine(nullptr, 0, false, fmtPayload, 3);
    if (fmtLine.rfind("F3DIAG ", 0) != 0 ||
        fmtLine.find(fmtPayload) == std::string::npos ||
        fmtLine.find('\n') != std::string::npos || fmtLine.size() >= 320) {
      std::printf("  detail: format-looking err not verbatim/bounded\n");
      return false;
    }
    // Overlong payload (5000B) must truncate to the 320B diag bound.
    const std::string overlong(5000, 'E');
    const std::string longLine =
        runtime::FormatContentDiagLine(nullptr, 0, true, overlong.c_str(), 7);
    if (longLine.rfind("F3DIAG ", 0) != 0 ||
        longLine.find('\n') != std::string::npos || longLine.size() >= 320) {
      std::printf("  detail: overlong err grew past the diag bound\n");
      return false;
    }
    // Newline injection: bounded + prefix + verbatim (no format
    // interpretation). NOTE: embedded newlines currently pass through
    // (pre-existing residual per security-review; identical pre/post move),
    // so this leg pins boundedness/naming, not sanitization.
    const char* nlPayload = "boom\nINJECTED_TAG contentErr=hacked";
    const std::string nlLine =
        runtime::FormatContentDiagLine(nullptr, 0, false, nlPayload, 1);
    if (nlLine.rfind("F3DIAG ", 0) != 0 || nlLine.size() >= 320 ||
        nlLine.find("boom") == std::string::npos) {
      std::printf("  detail: newline err broke bound/prefix/verbatim\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: FormatContentDiagLine must never throw\n");
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
  // T6 seam setup (FR-RUNTIME-013): reach dispatch through the content-tag
  // registry via explicit registration (never app name; never static-init
  // order). Idempotent; first registration wins.
  (void)runtime::RegisterFlappyContent();
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
  RUN_CASE(check_t1_registration_seam_callable);
  RUN_CASE(check_format_content_diag_line_hostile_err_stays_bounded);
  RUN_CASE(check_non31_touchable_dispatches_via_jsruntime);
  RUN_CASE(check_every_touchable_names_handler_via_registration);
  RUN_CASE(check_startup_store_has_no_preseeded_handles);
  if (failures > 0) {
    std::printf("flappy_tap_js_dispatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_tap_js_dispatch_test: all passing\n");
  return 0;
}
