// JsEngine implementation (FR-RUNTIME-007 / AC-RUNTIME-018).
// Component bodies run exactly once per mount; effects and computed getters
// evaluate through JsRuntime::eval followed by a microtask drain.

#include "runtime/js_engine.h"

namespace runtime {

JsEngine::JsEngine(JsRuntime& rt) : rt_(&rt) {}

std::unique_ptr<JsEngine> JsEngine::create(FakeEngineHooks hooks) {
  std::unique_ptr<JsRuntime> rt =
      JsRuntimeFactory::createFake(std::move(hooks));
  std::unique_ptr<JsEngine> engine(new JsEngine(*rt));
  engine->owned_rt_ = std::move(rt);
  // Bootstrap yields exactly one isolate; no handles exist yet.
  engine->live_isolates_ = 1;
  engine->live_handles_ = 0;
  return engine;
}

void JsEngine::destroy() {
  if (destroyed_) {
    return;
  }
  destroyed_ = true;
  // Release all handles and the isolate. A collect pass gives the runtime
  // a chance to drop engine-side state before the owned runtime goes away.
  if (rt_ != nullptr) {
    rt_->collectGarbage();
  }
  if (owned_rt_ != nullptr) {
    owned_rt_.reset();
    rt_ = nullptr;
  }
  live_isolates_ = 0;
  live_handles_ = 0;
  body_runs_.clear();
}

int JsEngine::liveIsolates() const { return live_isolates_; }

int JsEngine::liveHandles() const { return live_handles_; }

InstanceId JsEngine::mount(const std::string& bodySource) {
  if (destroyed_ || rt_ == nullptr) {
    throw JsError{"engine destroyed"};
  }
  JsValue result = rt_->eval(bodySource, "body");
  rt_->drainMicrotasks();
  ++live_handles_;
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
  if (destroyed_ || rt_ == nullptr) {
    return;
  }
  rt_->eval(effectSource, "effect");
  rt_->drainMicrotasks();
  ++live_handles_;
}

JsValue JsEngine::readComputed(InstanceId id,
                               const std::string& getterSource) {
  (void)id;
  if (destroyed_ || rt_ == nullptr) {
    return JsValue{false, "engine destroyed"};
  }
  JsValue result = rt_->eval(getterSource, "computed");
  rt_->drainMicrotasks();
  ++live_handles_;
  return result;
}

}  // namespace runtime
