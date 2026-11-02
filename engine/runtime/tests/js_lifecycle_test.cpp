// T6 contract tests — FR-RUNTIME-007 / AC-RUNTIME-020 (TC-RUNTIME-020).
// Engine create/destroy lifecycle: bootstrap creates exactly one isolate,
// destroy releases all handles and the isolate, double-destroy is a no-op.
// Extends T2's JsEngine (no new class).
// Hermetic: C++17 + runtime/js_engine.h + runtime/js_runtime.h only; fake
// engine is a test-local JsRuntime subclass (in-process counters), no
// Hermes headers, no network, no env/config reads, no filesystem, no live
// services. Default prod path is exercised via
// JsEngine::create(RealEngineParams{}) (real-or-fail-closed); the injected
// fake path uses the JsEngine(JsRuntime&) DI seam with a test-local fake.
// Binds ONLY to the T3/T4 `Interface:` lines; never to implementation.
#include <cstdio>
#include <memory>
#include <string>

#include "runtime/js_engine.h"
#include "runtime/js_runtime.h"

namespace {

// Test-only fake engine: in-process JsRuntime behind the JsEngine(JsRuntime&)
// DI seam. Counts evals, drains, collects. Lives in this test TU only —
// never in the `runtime` lib sources.
class FakeJsRuntime : public runtime::JsRuntime {
 public:
  runtime::JsValue eval(const std::string& source,
                        const std::string& origin) override {
    ++evals;
    (void)source;
    (void)origin;
    return runtime::JsValue{true, ""};
  }

  void drainMicrotasks() override { ++drains; }
  void collectGarbage() override { ++collects; }

  int evals = 0;
  int drains = 0;
  int collects = 0;
};

}  // namespace

// Named case: default prod create bootstraps exactly one isolate.
static bool check_create_yields_one_live_isolate() {
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(runtime::RealEngineParams{});
  if (!engine) return false;
  return engine->liveIsolates() == 1;
}

// Named case: destroy releases everything — liveIsolates and liveHandles
// both return 0 afterwards (injected-fake path with a live handle).
static bool check_destroy_releases_isolate_and_handles() {
  FakeJsRuntime fake;
  std::unique_ptr<runtime::JsEngine> engine(
      new runtime::JsEngine(fake));
  engine->mount("function body() {}");
  if (engine->liveHandles() == 0) return false;
  engine->destroy();
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: double-destroy is a no-op — never crashes, counters stay 0
// (injected-fake path with a live handle; default path covered by
// check_destroy_without_mount_leaves_zero).
static bool check_double_destroy_is_noop() {
  FakeJsRuntime fake;
  std::unique_ptr<runtime::JsEngine> engine(
      new runtime::JsEngine(fake));
  engine->mount("function body() {}");
  engine->destroy();
  try {
    engine->destroy();
  } catch (...) {
    return false;
  }
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: destroy without any mount still leaves zero counters
// (default prod path).
static bool check_destroy_without_mount_leaves_zero() {
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(runtime::RealEngineParams{});
  engine->destroy();
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: create/destroy/create cycle — a fresh engine bootstraps a
// new isolate after the previous one was destroyed (default prod path).
static bool check_recreate_after_destroy_bootstraps_again() {
  {
    std::unique_ptr<runtime::JsEngine> first =
        runtime::JsEngine::create(runtime::RealEngineParams{});
    if (first->liveIsolates() != 1) return false;
    first->destroy();
    if (first->liveIsolates() != 0) return false;
  }
  std::unique_ptr<runtime::JsEngine> second =
      runtime::JsEngine::create(runtime::RealEngineParams{});
  if (!second) return false;
  if (second->liveIsolates() != 1) return false;
  second->destroy();
  return second->liveIsolates() == 0 && second->liveHandles() == 0;
}

// Named case: engine is usable between create and destroy — mount evals
// exactly once through the injected test-local fake.
static bool check_mount_between_create_and_destroy_evals_once() {
  FakeJsRuntime fake;
  std::unique_ptr<runtime::JsEngine> engine(
      new runtime::JsEngine(fake));
  const int before = fake.evals;
  runtime::InstanceId id = engine->mount("function body() {}");
  if (id == 0) return false;
  if (fake.evals != before + 1) return false;
  if (engine->bodyRunCount(id) != 1) return false;
  engine->destroy();
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: default prod mount fail-closes — no embedded engine linked
// means mount throws JsError instead of answering success without work.
static bool check_default_mount_fail_closed_throws() {
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(runtime::RealEngineParams{});
  try {
    engine->mount("function body() {}");
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
  RUN_CASE(check_create_yields_one_live_isolate);
  RUN_CASE(check_destroy_releases_isolate_and_handles);
  RUN_CASE(check_double_destroy_is_noop);
  RUN_CASE(check_destroy_without_mount_leaves_zero);
  RUN_CASE(check_recreate_after_destroy_bootstraps_again);
  RUN_CASE(check_mount_between_create_and_destroy_evals_once);
  RUN_CASE(check_default_mount_fail_closed_throws);
  if (failures > 0) {
    std::printf("js_lifecycle_test: %d failing\n", failures);
    return 1;
  }
  std::printf("js_lifecycle_test: all passing\n");
  return 0;
}
