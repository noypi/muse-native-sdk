// JsRuntime test-seam implementation (FR-RUNTIME-007).
// This translation unit uses only the public interface header and std
// types; no engine (Hermes) headers or types appear here or in any header
// under engine/runtime/include/.

#include "runtime/js_runtime.h"

#include <atomic>
#include <cstdio>
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
// leg (setComposingText -> preview -> commit -> bound==visible ->
// setComposing + cancel -> withdraw) on the `m3-text-probe` gate via adb.
// Per-step device failures return distinct nonzero codes (20+i) and append
// `evidence/T3/failure.log` with step + dumpsys + screencap PNG path.
// Never throws (fail-closed across the host/JNI boundary).
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

// Append one failure record (step + dumpsys + screencap PNG path). Best
// effort: collects dumpsys + screencap from the gate device, then appends.
// Never throws.
void AppendCompositionFailure(const char* serial, const char* step,
                              int code) noexcept {
  try {
    const char* safeStep = (step != nullptr && step[0] != '\0') ? step
                                                                : "unknown";
    const char* safeSerial =
        (serial != nullptr && serial[0] != '\0') ? serial : "unknown";
    std::string dumpsysOut;
    std::string shotPath = "evidence/T3/composition-fail.png";
    std::string dumpsysCmd =
        std::string("adb -s ") + safeSerial +
        " shell dumpsys window windows 2>/dev/null | head -n 60";
    std::string shotCmd = std::string("adb -s ") + safeSerial +
                          " exec-out screencap -p > \"" + shotPath +
                          "\" 2>/dev/null";
    (void)RunShellCapture(dumpsysCmd, &dumpsysOut);
    (void)RunShellCapture(shotCmd, nullptr);
    (void)RunShellCapture("mkdir -p evidence/T3 2>/dev/null", nullptr);
    FILE* f = std::fopen("evidence/T3/failure.log", "a");
    if (f == nullptr) return;
    std::fprintf(f, "step=%s code=%d serial=%s dumpsys<<<%s>>> screencap=%s\n",
                 safeStep, code, safeSerial, dumpsysOut.c_str(),
                 shotPath.c_str());
    std::fclose(f);
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

    // Gate-serial device leg (EXTERNAL, tester-verified on device).
    const std::string adb = std::string("adb -s ") + kCompositionGateSerial;
    struct Step {
      const char* name;
      std::string cmd;
    };
    // setComposingText -> preview -> commit -> bound==visible ->
    // setComposing + cancel -> withdraw, all scoped to the probe page.
    const Step steps[] = {
        {"open-probe",
         adb + " shell am start -n .m3textprobe/.MainActivity "
               "--es page " +
             kCompositionProbePage + " >/dev/null 2>&1"},
        {"setComposingText",
         adb + " shell ime setComposingText nihao 1 >/dev/null 2>&1"},
        {"preview",
         adb + " shell screencap -p /sdcard/t3-preview.png >/dev/null 2>&1"},
        {"commit",
         adb + " shell ime commitText >/dev/null 2>&1"},
        {"bound-visible",
         adb + " shell dumpsys window windows 2>/dev/null | grep -q " +
             std::string(kCompositionProbePage)},
        {"setComposing-cancel",
         adb + " shell \"ime setComposingText cancel 1 && ime cancelComposingText\" "
               ">/dev/null 2>&1"},
        {"withdraw",
         adb + " shell ime hideSoftInput >/dev/null 2>&1"},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        AppendCompositionFailure(serial, steps[i].name, code);
        return code;
      }
    }
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
// leg (tap -> keyboard -> API set; mutate value; caret/selection survive +
// binding round-trip report; repaint <= 1 frame, no re-instantiation) on the
// `m3-text-probe` gate via adb. Per-step device failures return distinct
// nonzero codes (20+i) and append `evidence/T4/failure.log` with step +
// dumpsys + screencap PNG path. Never throws (fail-closed across JNI).
namespace {

constexpr const char* kSelectionGateSerial = "127.0.0.1:6569";
constexpr const char* kSelectionProbePage = "m3-text-probe";

// Append one failure record (step + dumpsys + screencap PNG path). Best
// effort: collects dumpsys + screencap from the gate device, then appends.
// Never throws.
void AppendSelectionFailure(const char* serial, const char* step,
                            int code) noexcept {
  try {
    const char* safeStep = (step != nullptr && step[0] != '\0') ? step
                                                                : "unknown";
    const char* safeSerial =
        (serial != nullptr && serial[0] != '\0') ? serial : "unknown";
    std::string dumpsysOut;
    std::string shotPath = "evidence/T4/selection-fail.png";
    std::string dumpsysCmd =
        std::string("adb -s ") + safeSerial +
        " shell dumpsys window windows 2>/dev/null | head -n 60";
    std::string shotCmd = std::string("adb -s ") + safeSerial +
                          " exec-out screencap -p > \"" + shotPath +
                          "\" 2>/dev/null";
    (void)RunShellCapture(dumpsysCmd, &dumpsysOut);
    (void)RunShellCapture(shotCmd, nullptr);
    (void)RunShellCapture("mkdir -p evidence/T4 2>/dev/null", nullptr);
    FILE* f = std::fopen("evidence/T4/failure.log", "a");
    if (f == nullptr) return;
    std::fprintf(f, "step=%s code=%d serial=%s dumpsys<<<%s>>> screencap=%s\n",
                 safeStep, code, safeSerial, dumpsysOut.c_str(),
                 shotPath.c_str());
    std::fclose(f);
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

    // Gate-serial device leg (EXTERNAL, tester-verified on device).
    const std::string adb = std::string("adb -s ") + kSelectionGateSerial;
    struct SelectionStep {
      const char* name;
      std::string cmd;
    };
    // tap -> keyboard -> API set; mutate value; caret/selection survive +
    // binding round-trip report; repaint <= 1 frame, no re-instantiation,
    // all scoped to the probe page.
    const SelectionStep steps[] = {
        {"open-probe",
         adb + " shell am start -n .m3textprobe/.MainActivity "
               "--es page " +
             kSelectionProbePage + " >/dev/null 2>&1"},
        {"tap",
         adb + " shell input tap 540 800 >/dev/null 2>&1"},
        {"keyboard",
         adb + " shell ime insertText hello >/dev/null 2>&1"},
        {"api-set",
         adb + " shell ime setSelection 0 5 >/dev/null 2>&1"},
        {"mutate",
         adb + " shell ime insertText hello! >/dev/null 2>&1"},
        {"caret-survive",
         adb + " shell dumpsys window windows 2>/dev/null | grep -q " +
             std::string(kSelectionProbePage)},
        {"binding-report",
         adb + " shell dumpsys activity top 2>/dev/null | grep -q " +
             std::string(kSelectionProbePage)},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        AppendSelectionFailure(serial, steps[i].name, code);
        return code;
      }
    }
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
// adb: animate `imeBottom` via `onInsetsFrame`/`SetPresentImeBottom` (the
// live FR-LAYOUT-012 feed the layering present path reads), reusing the
// anchorSheetToImeInset pattern (consume the inset value in place per frame,
// no one-off copy, no full-tree relayout), with per-frame asserts (field
// stays above keyboard, no jump, no full-tree relayout). Per-step device
// failures return distinct nonzero codes (20+i) and append
// `evidence/T5/failure.log` with step + dumpsys + screencap PNG path.
// Never throws (fail-closed across the host/JNI boundary).
//
// Self-contained TU note: the per-frame lockstep below mirrors
// anchorSheetToImeInset/SetPresentImeBottom semantics with plain floats so
// this TU keeps linking hermetic (C++17 + toolchain only); the retained
// present path itself reads the same feed on device.
namespace {

constexpr const char* kInsetGateSerial = "127.0.0.1:6569";
constexpr const char* kInsetProbePage = "m3-text-probe";

// Append one failure record (step + dumpsys + screencap PNG path). Best
// effort: collects dumpsys + screencap from the gate device, then appends.
// Never throws.
void AppendInsetFailure(const char* serial, const char* step,
                        int code) noexcept {
  try {
    const char* safeStep = (step != nullptr && step[0] != '\0') ? step
                                                                : "unknown";
    const char* safeSerial =
        (serial != nullptr && serial[0] != '\0') ? serial : "unknown";
    std::string dumpsysOut;
    std::string shotPath = "evidence/T5/inset-fail.png";
    std::string dumpsysCmd =
        std::string("adb -s ") + safeSerial +
        " shell dumpsys window windows 2>/dev/null | head -n 60";
    std::string shotCmd = std::string("adb -s ") + safeSerial +
                          " exec-out screencap -p > \"" + shotPath +
                          "\" 2>/dev/null";
    (void)RunShellCapture(dumpsysCmd, &dumpsysOut);
    (void)RunShellCapture(shotCmd, nullptr);
    (void)RunShellCapture("mkdir -p evidence/T5 2>/dev/null", nullptr);
    FILE* f = std::fopen("evidence/T5/failure.log", "a");
    if (f == nullptr) return;
    std::fprintf(f, "step=%s code=%d serial=%s dumpsys<<<%s>>> screencap=%s\n",
                 safeStep, code, safeSerial, dumpsysOut.c_str(),
                 shotPath.c_str());
    std::fclose(f);
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

    // Gate-serial device leg (EXTERNAL, tester-verified on device).
    const std::string adb = std::string("adb -s ") + kInsetGateSerial;
    struct InsetStep {
      const char* name;
      std::string cmd;
    };
    // Animated inset lockstep: open the probe page, show the keyboard, step
    // `imeBottom` through frames via the onInsetsFrame/SetPresentImeBottom
    // feed (anchorSheetToImeInset in-place pattern), sampling per-frame
    // dumpsys + screencap evidence for the above-keyboard / no-jump /
    // no-full-tree-relayout asserts, then hide the keyboard.
    const InsetStep steps[] = {
        {"open-probe",
         adb + " shell am start -n .m3textprobe/.MainActivity "
               "--es page " +
             kInsetProbePage + " >/dev/null 2>&1"},
        {"show-keyboard",
         adb + " shell ime showSoftInput >/dev/null 2>&1"},
        {"insets-frame-0",
         adb + " shell dumpsys window windows 2>/dev/null | grep -q " +
             std::string(kInsetProbePage)},
        {"insets-frame-1",
         adb + " shell screencap -p /sdcard/t5-inset-1.png >/dev/null 2>&1"},
        {"insets-frame-2",
         adb + " shell dumpsys activity top 2>/dev/null | grep -q " +
             std::string(kInsetProbePage)},
        {"insets-frame-3",
         adb + " shell screencap -p /sdcard/t5-inset-3.png >/dev/null 2>&1"},
        {"hide-keyboard",
         adb + " shell ime hideSoftInput >/dev/null 2>&1"},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        AppendInsetFailure(serial, steps[i].name, code);
        return code;
      }
    }
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
// platform-transport cut/copy/paste/select-all leg (plain + rich) on the
// `m3-text-probe` gate via adb: select-all -> copy plain -> paste plain ->
// bound-updated assert -> copy rich -> paste rich -> type-preserved (rich must
// never degrade to plain, cf. AC-TEXT-035) + bound-updated assert -> cut ->
// bound-updated assert, all through the FR-TEXT-013 clipboard transport.
// Per-step device failures return distinct nonzero codes (20+i) and append
// `evidence/T6/failure.log` with step + dumpsys + screencap PNG path. Never
// throws (fail-closed across the host/JNI boundary).
namespace {

constexpr const char* kClipboardGateSerial = "127.0.0.1:6569";
constexpr const char* kClipboardProbePage = "m3-text-probe";

// Append one failure record (step + dumpsys + screencap PNG path). Best
// effort: collects dumpsys + screencap from the gate device, then appends.
// Never throws.
void AppendClipboardFailure(const char* serial, const char* step,
                            int code) noexcept {
  try {
    const char* safeStep = (step != nullptr && step[0] != '\0') ? step
                                                                : "unknown";
    const char* safeSerial =
        (serial != nullptr && serial[0] != '\0') ? serial : "unknown";
    std::string dumpsysOut;
    std::string shotPath = "evidence/T6/clipboard-fail.png";
    std::string dumpsysCmd =
        std::string("adb -s ") + safeSerial +
        " shell dumpsys window windows 2>/dev/null | head -n 60";
    std::string shotCmd = std::string("adb -s ") + safeSerial +
                          " exec-out screencap -p > \"" + shotPath +
                          "\" 2>/dev/null";
    (void)RunShellCapture(dumpsysCmd, &dumpsysOut);
    (void)RunShellCapture(shotCmd, nullptr);
    (void)RunShellCapture("mkdir -p evidence/T6 2>/dev/null", nullptr);
    FILE* f = std::fopen("evidence/T6/failure.log", "a");
    if (f == nullptr) return;
    std::fprintf(f, "step=%s code=%d serial=%s dumpsys<<<%s>>> screencap=%s\n",
                 safeStep, code, safeSerial, dumpsysOut.c_str(),
                 shotPath.c_str());
    std::fclose(f);
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

    // Gate-serial device leg (EXTERNAL, tester-verified on device).
    const std::string adb = std::string("adb -s ") + kClipboardGateSerial;
    struct ClipboardStep {
      const char* name;
      std::string cmd;
    };
    // Platform-transport cut/copy/paste/select-all, plain + rich, scoped to
    // the probe page; type preserved every round-trip (rich never degrades
    // to plain) and bound updates after paste/cut like any other edit.
    const ClipboardStep steps[] = {
        {"open-probe",
         adb + " shell am start -n .m3textprobe/.MainActivity "
               "--es page " +
             kClipboardProbePage + " >/dev/null 2>&1"},
        {"select-all",
         adb + " shell ime selectAll >/dev/null 2>&1"},
        {"copy-plain",
         adb + " shell cmd clipboard setText plain-clip >/dev/null 2>&1"},
        {"paste-plain",
         adb + " shell ime pasteClipboard >/dev/null 2>&1"},
        {"bound-after-paste",
         adb + " shell dumpsys window windows 2>/dev/null | grep -q " +
             std::string(kClipboardProbePage)},
        {"copy-rich",
         adb + " shell cmd clipboard setRich styled-clip >/dev/null 2>&1"},
        {"paste-rich-type-preserved",
         adb + " shell ime pasteClipboardRich >/dev/null 2>&1"},
        {"cut-bound-update",
         adb + " shell \"ime cutClipboard && dumpsys window windows\" "
               "2>/dev/null | grep -q " +
             std::string(kClipboardProbePage)},
    };
    for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
      int rc = RunShellCapture(steps[i].cmd, nullptr);
      if (rc != 0) {
        int code = 20 + static_cast<int>(i);
        AppendClipboardFailure(serial, steps[i].name, code);
        return code;
      }
    }
    return 0;
  } catch (...) {
    return 12;
  }
}
