// T2 contract tests — FR-RUNTIME-007 / AC-RUNTIME-018 (TC-RUNTIME-018).
// Component bodies/effects/computed execute in the embedded engine; a body
// runs exactly once per instance.
// Hermetic: C++17 + runtime/js_engine.h + runtime/js_runtime.h only; fake
// JsRuntime is an in-process function-pointer table, no Hermes headers, no
// network, no env/config reads, no filesystem, no live services.
// Binds ONLY to the T2 `Interface:` line; never to implementation.
#include <cstdio>
#include <string>

#include "runtime/js_engine.h"
#include "runtime/js_runtime.h"

namespace {

// In-process fake engine: counts evals, can inject eval failure.
class FakeJsRuntime : public runtime::JsRuntime {
 public:
  explicit FakeJsRuntime(bool fail_eval = false) : fail_eval_(fail_eval) {}

  runtime::JsValue eval(const std::string& source,
                        const std::string& origin) override {
    ++evals_;
    last_source_ = source;
    if (fail_eval_) return runtime::JsValue{false, "fake eval failure"};
    return runtime::JsValue{true, ""};
  }

  void drainMicrotasks() override { ++drains_; }
  void collectGarbage() override {}

  int evals_ = 0;
  int drains_ = 0;
  bool fail_eval_ = false;
  std::string last_source_;
};

}  // namespace

// Named case: mount evals the body and returns a nonzero instance id.
static bool check_mount_returns_nonzero_id() {
  FakeJsRuntime rt;
  runtime::JsEngine engine(rt);
  runtime::InstanceId id = engine.mount("function body() {}");
  return id != 0;
}

// Named case: body runs exactly once — bodyRunCount==1 after mount + flush.
static bool check_body_runs_exactly_once() {
  FakeJsRuntime rt;
  runtime::JsEngine engine(rt);
  runtime::InstanceId id = engine.mount("function body() {}");
  if (id == 0) return false;
  return engine.bodyRunCount(id) == 1;
}

// Named case: two mounts yield distinct ids each with run count 1.
static bool check_two_mounts_distinct_ids_count_one_each() {
  FakeJsRuntime rt;
  runtime::JsEngine engine(rt);
  runtime::InstanceId a = engine.mount("body-a");
  runtime::InstanceId b = engine.mount("body-b");
  if (a == 0 || b == 0 || a == b) return false;
  return engine.bodyRunCount(a) == 1 && engine.bodyRunCount(b) == 1;
}

// Named case: runEffect executes its source through JsRuntime::eval.
static bool check_run_effect_evals_source() {
  FakeJsRuntime rt;
  runtime::JsEngine engine(rt);
  runtime::InstanceId id = engine.mount("body");
  const int before = rt.evals_;
  engine.runEffect(id, "effect-source");
  return rt.evals_ == before + 1 && rt.last_source_ == "effect-source";
}

// Named case: readComputed executes its getter through JsRuntime::eval.
static bool check_read_computed_evals_getter() {
  FakeJsRuntime rt;
  runtime::JsEngine engine(rt);
  runtime::InstanceId id = engine.mount("body");
  const int before = rt.evals_;
  runtime::JsValue v = engine.readComputed(id, "getter-source");
  if (!v.ok) return false;
  return rt.evals_ == before + 1 && rt.last_source_ == "getter-source";
}

// Named case: mount on eval failure throws JsError carrying a message.
static bool check_mount_failure_throws_js_error() {
  FakeJsRuntime rt(true /*fail_eval*/);
  runtime::JsEngine engine(rt);
  try {
    engine.mount("bogus((");
  } catch (const runtime::JsError& e) {
    return !std::string(e.message).empty();
  } catch (...) {
    return false;
  }
  return false;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_mount_returns_nonzero_id);
  RUN_CASE(check_body_runs_exactly_once);
  RUN_CASE(check_two_mounts_distinct_ids_count_one_each);
  RUN_CASE(check_run_effect_evals_source);
  RUN_CASE(check_read_computed_evals_getter);
  RUN_CASE(check_mount_failure_throws_js_error);
  if (failures > 0) {
    std::printf("js_execution_test: %d failing\n", failures);
    return 1;
  }
  std::printf("js_execution_test: all passing\n");
  return 0;
}
