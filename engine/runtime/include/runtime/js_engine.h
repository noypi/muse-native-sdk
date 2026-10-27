#pragma once

#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>

#include "runtime/js_runtime.h"

namespace runtime {

// Integer-like instance handle; 0 is reserved as invalid.
using InstanceId = std::uint32_t;
inline constexpr InstanceId kInvalidInstanceId = 0;

// Typed error thrown by JsEngine::mount when body evaluation fails.
struct JsError : public std::exception {
  std::string message;

  JsError() = default;
  explicit JsError(std::string msg) : message(std::move(msg)) {}

  const char* what() const noexcept override { return message.c_str(); }
};

// Executes component bodies/effects/computed in the embedded engine
// (FR-RUNTIME-007 / AC-RUNTIME-018). All script execution goes through the
// injected JsRuntime boundary followed by a microtask drain; no engine
// headers or types appear here.
class JsEngine {
 public:
  explicit JsEngine(JsRuntime& rt);

  InstanceId mount(const std::string& bodySource);
  int bodyRunCount(InstanceId id) const;
  void runEffect(InstanceId id, const std::string& effectSource);
  JsValue readComputed(InstanceId id, const std::string& getterSource);

 private:
  JsRuntime* rt_;
  InstanceId next_id_ = 1;
  std::unordered_map<InstanceId, int> body_runs_;
};

}  // namespace runtime
