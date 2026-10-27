#pragma once

#include <functional>
#include <memory>
#include <string>

namespace runtime {

// Result of evaluating a script through the engine boundary.
// Engine failure is reported as ok=false with a plain-string message;
// an exception never crosses the JsRuntime boundary.
struct JsValue {
  bool ok = false;
  std::string error;
};

// Embedded-JS interface (FR-RUNTIME-007 / AC-RUNTIME-019).
// Pure-virtual ABC using only std types so consumers compile without any
// engine headers. Backends (Hermes, fakes) implement this interface;
// every access to the engine goes through it.
class JsRuntime {
 public:
  virtual JsValue eval(const std::string& source,
                       const std::string& origin) = 0;
  virtual void drainMicrotasks() = 0;
  virtual void collectGarbage() = 0;
  virtual ~JsRuntime() = default;
};

// Test seam: in-process function-table-equivalent hooks behind which unit
// tests run a fake engine. No engine headers, libs, or types involved.
struct FakeEngineHooks {
  std::function<JsValue(const std::string& source, const std::string& origin)>
      eval;
  std::function<void()> drain;
  std::function<void()> collect;
};

// Factory for test doubles bound to the injected hooks above.
class JsRuntimeFactory {
 public:
  static std::unique_ptr<JsRuntime> createFake(FakeEngineHooks hooks);
};

}  // namespace runtime
