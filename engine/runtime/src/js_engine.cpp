// JsEngine implementation (FR-RUNTIME-007 / AC-RUNTIME-018).
// Component bodies run exactly once per mount; effects and computed getters
// evaluate through JsRuntime::eval followed by a microtask drain.

#include "runtime/js_engine.h"

namespace runtime {

JsEngine::JsEngine(JsRuntime& rt) : rt_(&rt) {}

InstanceId JsEngine::mount(const std::string& bodySource) {
  JsValue result = rt_->eval(bodySource, "body");
  rt_->drainMicrotasks();
  if (!result.ok) {
    throw JsError{result.error.empty() ? "body evaluation failed"
                                       : result.error};
  }
  const InstanceId id = next_id_++;
  if (next_id_ == kInvalidInstanceId) {
    ++next_id_;
  }
  body_runs_[id] = 1;
  return id;
}

int JsEngine::bodyRunCount(InstanceId id) const {
  const auto it = body_runs_.find(id);
  if (it == body_runs_.end()) {
    return 0;
  }
  return it->second;
}

void JsEngine::runEffect(InstanceId id, const std::string& effectSource) {
  (void)id;
  rt_->eval(effectSource, "effect");
  rt_->drainMicrotasks();
}

JsValue JsEngine::readComputed(InstanceId id,
                               const std::string& getterSource) {
  (void)id;
  JsValue result = rt_->eval(getterSource, "computed");
  rt_->drainMicrotasks();
  return result;
}

}  // namespace runtime
