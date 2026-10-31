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
