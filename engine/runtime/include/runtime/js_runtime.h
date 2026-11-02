#pragma once

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
// engine headers. Production backends implement this interface; every
// access to the engine goes through it.
class JsRuntime {
 public:
  virtual JsValue eval(const std::string& source,
                       const std::string& origin) = 0;
  virtual void drainMicrotasks() = 0;
  virtual void collectGarbage() = 0;
  virtual ~JsRuntime() = default;
};

// Parameters for the production runtime factory. Empty on lanes with no
// embedded engine linked: createReal then fail-closes instead of answering
// success without work.
struct RealEngineParams {};

// Factory for the production runtime behind the seam above. The default
// prod path runs the real engine when one is linked, otherwise fail-closed;
// it never returns a test double.
class JsRuntimeFactory {
 public:
  static std::unique_ptr<JsRuntime> createReal(RealEngineParams params);
};

}  // namespace runtime
