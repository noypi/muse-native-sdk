// JsRuntime test-seam implementation (FR-RUNTIME-007).
// This translation unit uses only the public interface header and std
// types; no engine (Hermes) headers or types appear here or in any header
// under engine/runtime/include/.

#include "runtime/js_runtime.h"

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
