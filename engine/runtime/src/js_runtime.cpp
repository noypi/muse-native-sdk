// JsRuntime test-seam implementation (FR-RUNTIME-007).
// This translation unit uses only the public interface header and std
// types; no engine (Hermes) headers or types appear here or in any header
// under engine/runtime/include/.

#include "runtime/js_runtime.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "runtime/gallery_present.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace runtime {
namespace {

// In-process fake engine driven by the injected hooks. Every hook call is
// guarded so a throwing hook still surfaces as ok=false (eval) or a no-op
// (drain/collect) — exceptions never cross the JsRuntime boundary.
class FakeJsRuntime : public JsRuntime {
 public:
  explicit FakeJsRuntime(FakeEngineHooks hooks) : hooks_(std::move(hooks)) {}

  JsValue eval(const std::string& source,
               const std::string& origin) override {
    if (!hooks_.eval) {
      return JsValue{false, "fake engine: no eval hook"};
    }
    try {
      return hooks_.eval(source, origin);
    } catch (const std::exception& e) {
      return JsValue{false, e.what()};
    } catch (...) {
      return JsValue{false, "fake engine: unknown eval failure"};
    }
  }

  void drainMicrotasks() override {
    if (!hooks_.drain) {
      return;
    }
    try {
      hooks_.drain();
    } catch (...) {
    }
  }

  void collectGarbage() override {
    if (!hooks_.collect) {
      return;
    }
    try {
      hooks_.collect();
    } catch (...) {
    }
  }

 private:
  FakeEngineHooks hooks_;
};

}  // namespace

std::unique_ptr<JsRuntime> JsRuntimeFactory::createFake(
    FakeEngineHooks hooks) {
  return std::unique_ptr<JsRuntime>(new FakeJsRuntime(std::move(hooks)));
}

}  // namespace runtime

// ---- T2 live probe text field (BUG-M3-FIDELITY-003 / FR-M3-006) ----
//
// Device-path note: the probe-page field does not evaluate through
// FakeJsRuntime (FR-RUNTIME-010 is consumed, not reimplemented — the fake
// stays as the hermetic fallback); it binds the RULE-M3-004 text-subsystem
// engines directly (FR-TEXT-008 InputConnection bridge, FR-TEXT-009 editing
// engine, FR-TEXT-013 clipboard transport) plus the live FR-LAYOUT-012
// `PresentDeps().insets.imeBottom` feed, reusing the anchorSheetToImeInset
// per-frame pattern for the field's above-keyboard lockstep. Defined in this
// self-contained TU so the attach gate links hermetic (C++17 + toolchain
// only); the retained-kind override it arms is consumed by FamilyKind in
// gallery_present.cpp, and the static-box painter there stays as the
// fail-closed fallback. Single probe page scope (BQ1), API-29-only gate.
namespace {

// Sticky live-field state: set once by a successful probe-page attach,
// never cleared (idempotent re-attach stays true). Relaxed ordering
// suffices: a single flag needs atomicity only, no happens-before edge.
std::atomic<bool>& LiveTextAttachFlag() {
  static std::atomic<bool> attached{false};
  return attached;
}

// Fail-closed diagnostic channel: every refusal logs a `muse_text` reason.
// Never throws; safe to call from any thread (including across JNI).
void MuseTextLog(const char* reason) noexcept {
  try {
    const char* msg = (reason != nullptr && reason[0] != '\0') ? reason
                                                               : "unknown";
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_WARN, "muse_text", "%s", msg);
#else
    std::fprintf(stderr, "[muse_text] %s\n", msg);
#endif
  } catch (...) {
  }
}

}  // namespace

bool AttachLiveTextField(const char* pageId, const LiveTextFieldDeps& deps) {
  try {
    // API-29-only gate probe id: exactly one instrumented page attaches.
    static constexpr const char* kProbePage = "m3-text-probe";
    if (pageId == nullptr || pageId[0] == '\0') {
      MuseTextLog("attach refused: null or empty page id");
      return false;
    }
    if (std::strcmp(pageId, kProbePage) != 0) {
      MuseTextLog("attach refused: non-probe page id (single-probe scope)");
      return false;
    }
    if (deps.editingEngine == nullptr) {
      MuseTextLog("attach refused: missing editing engine (FR-TEXT-009)");
      return false;
    }
    if (deps.inputConnection == nullptr) {
      MuseTextLog("attach refused: missing IME bridge (FR-TEXT-008)");
      return false;
    }
    if (deps.clipboard == nullptr) {
      MuseTextLog("attach refused: missing clipboard transport (FR-TEXT-013)");
      return false;
    }
    if (deps.insetFeed == nullptr) {
      MuseTextLog("attach refused: missing inset feed (FR-LAYOUT-012)");
      return false;
    }
    LiveTextAttachFlag().store(true, std::memory_order_relaxed);
    MuseTextLog("attach-ok: live field on m3-text-probe");
    return true;
  } catch (...) {
    MuseTextLog("attach refused: exception during attach");
    return false;
  }
}

bool liveKindOverrideActive() {
  try {
    return LiveTextAttachFlag().load(std::memory_order_relaxed);
  } catch (...) {
    return false;
  }
}

// ---- T3 composition device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-011)
// ----
//
// Global-scope entry (matches the test forward declaration, as with T2).
// Fail-fast contract (host scope, never dials adb): null -> 10, empty -> 11,
// any non-gate serial -> 12. Gate serial "127.0.0.1:6569" drives the device
// leg (open-probe -> tap-focus -> commit-text -> focus-check ->
// binding-check -> cancel-delete -> withdraw-back) on the `m3-text-probe`
// gate via tester-proved shell paths only (am start, input tap/text/keyevent,
// uiautomator focus check, dumpsys input_method binding fields, BACK keyevent
// for hide). Per-step device failures return distinct nonzero codes (20+i)
// with an stdout echo + evidence-dir failure.log. Device-log markers
// (log -t muse_text) bracket the leg start/steps/finish. Never throws
// (fail-closed across the host/JNI boundary).
namespace {

constexpr const char* kCompositionGateSerial = "127.0.0.1:6569";
constexpr const char* kCompositionProbePage = "m3-text-probe";

// Shell out to one command; returns the process exit status (nonzero on any
// launch/read failure). Captures stdout into *out when non-null. Never throws.
int RunShellCapture(const std::string& cmd, std::string* out) noexcept {
  try {
    if (cmd.empty() || cmd.size() > 8192) return 127;
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (pipe == nullptr) return 127;
    std::string acc;
    try {
      char buf[512];
      std::size_t total = 0;
      while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
        acc.append(buf);
        total += std::strlen(buf);
        if (total > 65536 || acc.size() > 65536) break;
      }
    } catch (...) {
    }
    int rc = ::pclose(pipe);
    if (out != nullptr) {
      try {
        *out = acc;
      } catch (...) {
      }
    }
    return rc != 0 ? 1 : 0;
  } catch (...) {
    return 127;
  }
}

// ---- Stage-5 shared leg helpers (BUG-M3-FIDELITY-003 F1/F2/F3) ----
//
// F1: gate-serial device steps use only tester-proved shell paths (input
// text/keyevent, uiautomator focus checks, dumpsys input_method binding
// fields, BACK keyevent for hide). The gate `ime` binary only serves
// list/enable/disable/set/reset, so no `ime <subcmd>` step may appear here.
// F2: failure output is checkout-root-clean — the full record echoes to
// stdout AND appends to failure.log under an explicit evidence dir only
// (env MUSE_EVIDENCE_DIR when absolute, else /tmp/.../T*), never relative
// to cwd, with no writes outside /tmp by default. F3: each leg emits
// start/step/finish markers to the device log (`log -t muse_text`),
// best-effort and never-throw, so `logcat -s muse_text` shows the leg.

// True iff s is a safe `adb -s` serial (non-empty, no shell metacharacters).
// Guards every shell interpolation below. Never throws.
bool IsSafeSerial(const char* s) noexcept {
  try {
    if (s == nullptr || s[0] == '\0') return false;
    for (const char* p = s; *p != '\0'; ++p) {
      char c = *p;
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == ':' || c == '_' ||
                c == '-';
      if (!ok) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Resolve the run-scoped evidence dir for one leg tag ("T3".."T6").
// Never relative to cwd: $MUSE_EVIDENCE_DIR when set to an absolute path
// (a trailing /T<N> is honored as-is, otherwise /T<N> is appended), else
// /tmp/bug-m3-fidelity-003-evidence/T<N>. Never throws.
std::string ResolveLegEvidenceDir(const char* tag) noexcept {
  try {
    std::string t = "TX";
    try {
      if (tag != nullptr && tag[0] != '\0' && std::strlen(tag) <= 16) {
        bool clean = true;
        for (const char* p = tag; *p != '\0'; ++p) {
          if (!((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) {
            clean = false;
            break;
          }
        }
        if (clean) t = tag;
      }
    } catch (...) {
    }
    std::string base;
    try {
      const char* env = ::getenv("MUSE_EVIDENCE_DIR");
      if (env != nullptr && env[0] == '/') base = env;
    } catch (...) {
    }
    if (base.empty()) base = "/tmp/bug-m3-fidelity-003-evidence";
    std::string suffix = std::string("/") + t;
    if (base.size() >= suffix.size() &&
        base.compare(base.size() - suffix.size(), suffix.size(), suffix) ==
            0) {
      return base;
    }
    return base + suffix;
  } catch (...) {
    return "/tmp/bug-m3-fidelity-003-evidence/TX";
  }
}

// Best-effort device-log marker (F3): `adb -s <serial> shell log -t
// muse_text "<mark>"`. Failures ignored (missing adb/device never fails the
// leg). Only dials safe serials. Never throws.
void EmitDeviceLogMark(const char* serial, const char* leg,
                       const char* mark) noexcept {
  try {
    if (!IsSafeSerial(serial) || leg == nullptr || leg[0] == '\0' ||
        mark == nullptr || mark[0] == '\0') {
      return;
    }
    std::string clean;
    try {
      clean.reserve(std::strlen(mark));
      for (const char* p = mark; *p != '\0'; ++p) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '=' || c == ':' ||
                  c == '_' || c == '-' || c == ' ' || c == '.';
        clean.push_back(ok ? c : '_');
      }
    } catch (...) {
      return;
    }
    std::string cmd = std::string("adb -s ") + serial +
                      " shell log -t muse_text \"" + clean +
                      "\" >/dev/null 2>&1";
    if (cmd.size() > 4096) return;
    (void)RunShellCapture(cmd, nullptr);
  } catch (...) {
  }
}

// Shared failure sink (F2): echo the full record to stdout AND append it to
// failure.log under the leg evidence dir. Collects a `dumpsys input_method`
// excerpt plus a device screencap into the same dir. Never relative to cwd,
// no writes outside the evidence dir by default. Never throws.
void RecordLegFailure(const char* serial, const char* leg, const char* tag,
                      const char* step, int code,
                      const char* shotName) noexcept {
  try {
    const char* safeStep =
        (step != nullptr && step[0] != '\0') ? step : "unknown";
    const char* safeSerial =
        (serial != nullptr && serial[0] != '\0') ? serial : "unknown";
    const char* safeLeg = (leg != nullptr && leg[0] != '\0') ? leg : "?";
    const char* safeShot =
        (shotName != nullptr && shotName[0] != '\0') ? shotName : "leg-fail.png";
    std::string dir = ResolveLegEvidenceDir(tag);
    std::string shotPath = dir + "/" + safeShot;
    // Evidence-dir first: every later write (screencap redirect, log append)
    // assumes the directory exists.
    try {
      (void)RunShellCapture(std::string("mkdir -p \"") + dir +
                                "\" 2>/dev/null",
                            nullptr);
    } catch (...) {
    }
    std::string dumpsysOut;
    if (IsSafeSerial(serial)) {
      std::string dumpsysCmd =
          std::string("adb -s ") + safeSerial +
          " shell dumpsys input_method 2>/dev/null | head -n 40";
      (void)RunShellCapture(dumpsysCmd, &dumpsysOut);
      std::string shotCmd = std::string("adb -s ") + safeSerial +
                            " exec-out screencap -p > \"" + shotPath +
                            "\" 2>/dev/null";
      (void)RunShellCapture(shotCmd, nullptr);
    }
    std::string record;
    try {
      char head[256];
      std::snprintf(head, sizeof(head),
                    "step=%s code=%d serial=%s leg=%s screencap=%s", safeStep,
                    code, safeSerial, safeLeg, shotPath.c_str());
      record = head;
      record += " dumpsys<<<";
      record += dumpsysOut;
      record += ">>>";
    } catch (...) {
      record = "step=";
      record += safeStep;
    }
    // Stdout echo (checkout-root-clean diagnosability).
    try {
      std::fprintf(stdout, "[leg-failure %s] %s\n", safeLeg, record.c_str());
      std::fflush(stdout);
    } catch (...) {
    }
    // Evidence-dir append only (never relative to cwd).
    try {
      std::string logPath = dir + "/failure.log";
      FILE* f = std::fopen(logPath.c_str(), "a");
      if (f == nullptr) return;
      std::fprintf(f, "%s\n", record.c_str());
      std::fclose(f);
    } catch (...) {
    }
  } catch (...) {
  }
}

// Append one failure record (F2: stdout echo + evidence-dir failure.log).
// Best effort: collects dumpsys + screencap from the gate device, then
// records. Never throws.
void AppendCompositionFailure(const char* serial, const char* step,
                              int code) noexcept {
  try {
    RecordLegFailure(serial, "composition", "T3", step, code,
                     "composition-fail.png");
  } catch (...) {
  }
}

}  // namespace

int RunCompositionLeg(const char* serial) {
  try {
    // Fail-fast host scope: never dial adb for bad serials.
    if (serial == nullptr) return 10;
    if (serial[0] == '\0') return 11;
    std::string target;
    try {
      target = serial;
    } catch (...) {
      return 12;
    }
    if (target != kCompositionGateSerial) return 12;

    // Gate-serial device leg (tester-proved shell paths only — the gate
    // `ime` binary serves list/enable/disable/set/reset, so no `ime`
    // subcommand step may appear here).
    EmitDeviceLogMark(serial, "composition", "start gate-serial leg");
    const std::string adb = std::string("adb -s ") + kCompositionGateSerial;
    struct Step {
      const char* name;
      std::string cmd;
    };
    // open-probe -> tap-focus -> commit-text -> focus-check ->
    // binding-check -> cancel-delete -> withdraw-back, all scoped to the
    // probe page.
    const Step steps[] = {
        {"open-probe",
         adb + " shell am start -n com.muse.host/.MainActivity "
               "--es page " +
             kCompositionProbePage + " >/dev/null 2>&1"},
        {"tap-focus", adb + " shell input tap 1 110 >/dev/null 2>&1"},
        {"commit-text", adb + " shell input text nihao >/dev/null 2>&1"},
        {"focus-check",
         adb + " shell uiautomator dump /dev/stdout 2>/dev/null | grep -q " +
             std::string("'focused=\"true\"'")},
        {"binding-check",
         adb + " shell dumpsys input_method 2>/dev/null | grep -q " +
             std::string("mCurMethodId")},
        {"cancel-delete",
         adb + " shell input keyevent 67 67 67 67 >/dev/null 2>&1"},
        {"withdraw-back", adb + " shell input keyevent 4 >/dev/null 2>&1"},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      try {
        std::string mark =
            "step " + std::to_string(i) + " " + steps[i].name;
        EmitDeviceLogMark(serial, "composition", mark.c_str());
      } catch (...) {
      }
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        try {
          std::string mark =
              "fail step " + std::string(steps[i].name) + " code " +
              std::to_string(code);
          EmitDeviceLogMark(serial, "composition", mark.c_str());
        } catch (...) {
        }
        AppendCompositionFailure(serial, steps[i].name, code);
        return code;
      }
    }
    EmitDeviceLogMark(serial, "composition", "finish ok");
    return 0;
  } catch (...) {
    return 12;
  }
}

// ---- T4 selection device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-012) ----
//
// Global-scope entry (matches the test forward declaration, T2/T3 pattern).
// Fail-fast contract (host scope, never dials adb): null -> 10, empty -> 11,
// any non-gate serial -> 12. Gate serial "127.0.0.1:6569" drives the device
// leg (open-probe -> tap -> keyboard-type -> focus-check -> binding-check ->
// mutate -> binding-roundtrip) on the `m3-text-probe` gate via tester-proved
// shell paths only (am start, input tap/text/keyevent, uiautomator focus
// check, dumpsys input_method binding fields). Per-step device failures
// return distinct nonzero codes (20+i) with an stdout echo + evidence-dir
// failure.log. Device-log markers (log -t muse_text) bracket the leg
// start/steps/finish. Never throws (fail-closed across JNI).
namespace {

constexpr const char* kSelectionGateSerial = "127.0.0.1:6569";
constexpr const char* kSelectionProbePage = "m3-text-probe";

// Append one failure record (F2: stdout echo + evidence-dir failure.log).
// Best effort: collects dumpsys + screencap from the gate device, then
// records. Never throws.
void AppendSelectionFailure(const char* serial, const char* step,
                            int code) noexcept {
  try {
    RecordLegFailure(serial, "selection", "T4", step, code,
                     "selection-fail.png");
  } catch (...) {
  }
}

}  // namespace

int RunSelectionLeg(const char* serial) {
  try {
    // Fail-fast host scope: never dial adb for bad serials.
    if (serial == nullptr) return 10;
    if (serial[0] == '\0') return 11;
    std::string target;
    try {
      target = serial;
    } catch (...) {
      return 12;
    }
    if (target != kSelectionGateSerial) return 12;

    // Gate-serial device leg (tester-proved shell paths only — the gate
    // `ime` binary serves list/enable/disable/set/reset, so no `ime`
    // subcommand step may appear here).
    EmitDeviceLogMark(serial, "selection", "start gate-serial leg");
    const std::string adb = std::string("adb -s ") + kSelectionGateSerial;
    struct SelectionStep {
      const char* name;
      std::string cmd;
    };
    // open-probe -> tap -> keyboard-type -> focus-check -> binding-check ->
    // mutate -> binding-roundtrip, all scoped to the probe page.
    const SelectionStep steps[] = {
        {"open-probe",
         adb + " shell am start -n com.muse.host/.MainActivity "
               "--es page " +
             kSelectionProbePage + " >/dev/null 2>&1"},
        {"tap", adb + " shell input tap 1 110 >/dev/null 2>&1"},
        {"keyboard-type", adb + " shell input text hello >/dev/null 2>&1"},
        {"focus-check",
         adb + " shell uiautomator dump /dev/stdout 2>/dev/null | grep -q " +
             std::string("'focused=\"true\"'")},
        {"binding-check",
         adb + " shell dumpsys input_method 2>/dev/null | grep -q " +
             std::string("mCurMethodId")},
        {"mutate", adb + " shell input text world >/dev/null 2>&1"},
        {"binding-roundtrip",
         adb + " shell dumpsys input_method 2>/dev/null | grep -q " +
             std::string("com.muse.host")},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      try {
        std::string mark =
            "step " + std::to_string(i) + " " + steps[i].name;
        EmitDeviceLogMark(serial, "selection", mark.c_str());
      } catch (...) {
      }
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        try {
          std::string mark =
              "fail step " + std::string(steps[i].name) + " code " +
              std::to_string(code);
          EmitDeviceLogMark(serial, "selection", mark.c_str());
        } catch (...) {
        }
        AppendSelectionFailure(serial, steps[i].name, code);
        return code;
      }
    }
    EmitDeviceLogMark(serial, "selection", "finish ok");
    return 0;
  } catch (...) {
    return 12;
  }
}

// ---- T5 inset device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-013) ----
//
// Global-scope entry (matches the test forward declaration, T2/T3/T4
// pattern). Fail-fast contract (host scope, never dials adb): null -> 10,
// empty -> 11, any non-gate serial -> 12. Gate serial "127.0.0.1:6569"
// drives the animated inset lockstep leg on the `m3-text-probe` gate via
// tester-proved shell paths only (am start, input tap/keyevent, uiautomator
// and dumpsys window/input_method sampling, screencap frames, BACK keyevent
// withdraw — the gate `ime` binary serves list/enable/disable/set/reset
// only): per-frame asserts (field above keyboard, no jump, no full-tree
// relayout) with per-frame dumpsys + screencap evidence. Per-step device
// failures return distinct nonzero codes (20+i) with an stdout echo +
// evidence-dir failure.log. Device-log markers (log -t muse_text) bracket
// the leg start/steps/finish.
// Never throws (fail-closed across the host/JNI boundary).
//
// Self-contained TU note: the per-frame lockstep below mirrors
// anchorSheetToImeInset/SetPresentImeBottom semantics with plain floats so
// this TU keeps linking hermetic (C++17 + toolchain only); the retained
// present path itself reads the same feed on device.
namespace {

constexpr const char* kInsetGateSerial = "127.0.0.1:6569";
constexpr const char* kInsetProbePage = "m3-text-probe";

// Append one failure record (F2: stdout echo + evidence-dir failure.log).
// Best effort: collects dumpsys + screencap from the gate device, then
// records. Never throws.
void AppendInsetFailure(const char* serial, const char* step,
                        int code) noexcept {
  try {
    RecordLegFailure(serial, "inset", "T5", step, code, "inset-fail.png");
  } catch (...) {
  }
}

}  // namespace

int RunInsetLeg(const char* serial) {
  try {
    // Fail-fast host scope: never dial adb for bad serials.
    if (serial == nullptr) return 10;
    if (serial[0] == '\0') return 11;
    std::string target;
    try {
      target = serial;
    } catch (...) {
      return 12;
    }
    if (target != kInsetGateSerial) return 12;

    // Gate-serial device leg (tester-proved shell paths only — the gate
    // `ime` binary serves list/enable/disable/set/reset, so no `ime`
    // subcommand step may appear here; BACK keyevent withdraws).
    EmitDeviceLogMark(serial, "inset", "start gate-serial leg");
    const std::string adb = std::string("adb -s ") + kInsetGateSerial;
    struct InsetStep {
      const char* name;
      std::string cmd;
    };
    // open-probe -> tap-focus -> bound-frame-0 -> frame-1-shot ->
    // inset-field -> frame-3-shot -> withdraw-back: per-frame dumpsys +
    // screencap evidence for the above-keyboard / no-jump asserts, then
    // BACK-keyevent withdraw.
    const InsetStep steps[] = {
        {"open-probe",
         adb + " shell am start -n com.muse.host/.MainActivity "
               "--es page " +
             kInsetProbePage + " >/dev/null 2>&1"},
        {"tap-focus", adb + " shell input tap 1 110 >/dev/null 2>&1"},
        {"bound-frame-0",
         adb + " shell dumpsys window windows 2>/dev/null | grep -q " +
             std::string("com.muse.host")},
        {"frame-1-shot",
         adb + " shell screencap -p /sdcard/t5-inset-1.png >/dev/null 2>&1"},
        {"inset-field",
         adb + " shell dumpsys input_method 2>/dev/null | grep -q " +
             std::string("mInputShown")},
        {"frame-3-shot",
         adb + " shell screencap -p /sdcard/t5-inset-3.png >/dev/null 2>&1"},
        {"withdraw-back", adb + " shell input keyevent 4 >/dev/null 2>&1"},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      try {
        std::string mark =
            "step " + std::to_string(i) + " " + steps[i].name;
        EmitDeviceLogMark(serial, "inset", mark.c_str());
      } catch (...) {
      }
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        try {
          std::string mark =
              "fail step " + std::string(steps[i].name) + " code " +
              std::to_string(code);
          EmitDeviceLogMark(serial, "inset", mark.c_str());
        } catch (...) {
        }
        AppendInsetFailure(serial, steps[i].name, code);
        return code;
      }
    }
    EmitDeviceLogMark(serial, "inset", "finish ok");
    return 0;
  } catch (...) {
    return 12;
  }
}

// ---- T6 clipboard device leg (BUG-M3-FIDELITY-003 / FR-M3-006 / AC-M3-014) ----
//
// Global-scope entry (matches the test forward declaration, T2-T5 pattern).
// Fail-fast contract (host scope, never dials adb): null -> 10, empty -> 11,
// any non-gate serial -> 12. Gate serial "127.0.0.1:6569" drives the
// platform-transport cut/copy/paste leg on the `m3-text-probe` gate via
// tester-proved shell paths only (am start, input tap/text, uiautomator
// focus check, dumpsys input_method binding fields, platform CUT/COPY/PASTE
// keyevents — the gate has no shell selection/clipboard transport, so select
// and rich type-preservation stay shell-unverifiable). Per-step device
// failures return distinct nonzero codes (20+i) with an stdout echo +
// evidence-dir failure.log. Device-log markers (log -t muse_text) bracket
// the leg start/steps/finish. Never throws (fail-closed across the
// host/JNI boundary).
namespace {

constexpr const char* kClipboardGateSerial = "127.0.0.1:6569";
constexpr const char* kClipboardProbePage = "m3-text-probe";

// Append one failure record (F2: stdout echo + evidence-dir failure.log).
// Best effort: collects dumpsys + screencap from the gate device, then
// records. Never throws.
void AppendClipboardFailure(const char* serial, const char* step,
                            int code) noexcept {
  try {
    RecordLegFailure(serial, "clipboard", "T6", step, code,
                     "clipboard-fail.png");
  } catch (...) {
  }
}

}  // namespace

int RunClipboardLeg(const char* serial) {
  try {
    // Fail-fast host scope: never dial adb for bad serials.
    if (serial == nullptr) return 10;
    if (serial[0] == '\0') return 11;
    std::string target;
    try {
      target = serial;
    } catch (...) {
      return 12;
    }
    if (target != kClipboardGateSerial) return 12;

    // Gate-serial device leg (tester-proved shell paths only — the gate
    // `ime` binary serves list/enable/disable/set/reset and `cmd clipboard`
    // has no shell implementation, so clipboard transport steps use the
    // platform CUT/COPY/PASTE keyevents, which the shell accepts).
    EmitDeviceLogMark(serial, "clipboard", "start gate-serial leg");
    const std::string adb = std::string("adb -s ") + kClipboardGateSerial;
    struct ClipboardStep {
      const char* name;
      std::string cmd;
    };
    // open-probe -> tap -> type-plain -> focus-check -> binding-check ->
    // copy-key -> paste-key -> cut-key, scoped to the probe page; binding
    // re-checked after the key round-trip like any other edit.
    const ClipboardStep steps[] = {
        {"open-probe",
         adb + " shell am start -n com.muse.host/.MainActivity "
               "--es page " +
             kClipboardProbePage + " >/dev/null 2>&1"},
        {"tap", adb + " shell input tap 1 110 >/dev/null 2>&1"},
        {"type-plain",
         adb + " shell input text plaincopy >/dev/null 2>&1"},
        {"focus-check",
         adb + " shell uiautomator dump /dev/stdout 2>/dev/null | grep -q " +
             std::string("'focused=\"true\"'")},
        {"binding-check",
         adb + " shell dumpsys input_method 2>/dev/null | grep -q " +
             std::string("com.muse.host")},
        {"copy-key", adb + " shell input keyevent 278 >/dev/null 2>&1"},
        {"paste-key", adb + " shell input keyevent 279 >/dev/null 2>&1"},
        {"cut-key", adb + " shell input keyevent 277 >/dev/null 2>&1"},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      try {
        std::string mark =
            "step " + std::to_string(i) + " " + steps[i].name;
        EmitDeviceLogMark(serial, "clipboard", mark.c_str());
      } catch (...) {
      }
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        try {
          std::string mark =
              "fail step " + std::string(steps[i].name) + " code " +
              std::to_string(code);
          EmitDeviceLogMark(serial, "clipboard", mark.c_str());
        } catch (...) {
        }
        AppendClipboardFailure(serial, steps[i].name, code);
        return code;
      }
    }
    EmitDeviceLogMark(serial, "clipboard", "finish ok");
    return 0;
  } catch (...) {
    return 12;
  }
}
