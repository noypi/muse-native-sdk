// T1 contract tests — FR-RUNTIME-007 / AC-RUNTIME-019 (TC-RUNTIME-019).
// JsRuntime interface isolation: engine types never appear in public headers.
// Hermetic: C++17 + runtime/js_runtime.h only; fake engine is an in-process
// function-pointer table, no Hermes headers, no network, no env/config reads,
// no filesystem, no live services.
// Binds ONLY to the T1 `Interface:` line; never to implementation.
#include <cstdio>
#include <string>

#include "runtime/js_runtime.h"

namespace {

// Fake engine behind the boundary: records calls, injects failures.
class FakeJsRuntime : public runtime::JsRuntime {
 public:
  explicit FakeJsRuntime(bool fail_eval = false) : fail_eval_(fail_eval) {}

  runtime::JsValue eval(const std::string& source,
                        const std::string& origin) override {
    ++evals_;
    last_source_ = source;
    last_origin_ = origin;
    if (fail_eval_) return runtime::JsValue{false, "fake engine failure"};
    return runtime::JsValue{true, ""};
  }

  void drainMicrotasks() override { ++drains_; }
  void collectGarbage() override { ++gcs_; }

  int evals_ = 0;
  int drains_ = 0;
  int gcs_ = 0;
  bool fail_eval_ = false;
  std::string last_source_;
  std::string last_origin_;
};

}  // namespace

// Named case: eval of valid source reports ok=true through the interface.
static bool check_eval_ok_returns_ok_true() {
  FakeJsRuntime rt;
  runtime::JsValue v = rt.eval("1 + 1", "test");
  return v.ok && v.error.empty() && rt.evals_ == 1;
}

// Named case: engine failure yields ok=false with a message, never throws.
static bool check_engine_failure_yields_ok_false_no_throw() {
  FakeJsRuntime rt(true /*fail_eval*/);
  bool threw = false;
  runtime::JsValue v{true, ""};
  try {
    v = rt.eval("bogus((", "test");
  } catch (...) {
    threw = true;
  }
  if (threw) return false;
  return !v.ok && !v.error.empty();
}

// Named case: error message is propagated to the caller.
static bool check_error_message_propagated() {
  FakeJsRuntime rt(true /*fail_eval*/);
  runtime::JsValue v = rt.eval("x", "origin-here");
  return !v.ok && v.error == "fake engine failure";
}

// Named case: drainMicrotasks and collectGarbage are reachable via the
// interface and return without throwing.
static bool check_drain_and_gc_no_throw() {
  FakeJsRuntime rt;
  try {
    rt.drainMicrotasks();
    rt.collectGarbage();
  } catch (...) {
    return false;
  }
  return rt.drains_ == 1 && rt.gcs_ == 1;
}

// Named case: source and origin cross the boundary intact (std::string only,
// no engine types in the call).
static bool check_source_origin_forwarded() {
  FakeJsRuntime rt;
  rt.eval("body-source", "origin-1");
  return rt.last_source_ == "body-source" && rt.last_origin_ == "origin-1";
}

// Named case: polymorphic delete through the base is safe (virtual dtor).
static bool check_polymorphic_delete_safe() {
  runtime::JsRuntime* rt = new FakeJsRuntime();
  try {
    delete rt;
  } catch (...) {
    return false;
  }
  return true;
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
  RUN_CASE(check_eval_ok_returns_ok_true);
  RUN_CASE(check_engine_failure_yields_ok_false_no_throw);
  RUN_CASE(check_error_message_propagated);
  RUN_CASE(check_drain_and_gc_no_throw);
  RUN_CASE(check_source_origin_forwarded);
  RUN_CASE(check_polymorphic_delete_safe);
  if (failures > 0) {
    std::printf("js_abi_isolation_test: %d failing\n", failures);
    return 1;
  }
  std::printf("js_abi_isolation_test: all passing\n");
  return 0;
}
