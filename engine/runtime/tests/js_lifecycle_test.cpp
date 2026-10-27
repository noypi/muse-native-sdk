// T6 contract tests — FR-RUNTIME-007 / AC-RUNTIME-020 (TC-RUNTIME-020).
// Engine create/destroy lifecycle: bootstrap creates exactly one isolate,
// destroy releases all handles and the isolate, double-destroy is a no-op.
// Extends T2's JsEngine (no new class).
// Hermetic: C++17 + runtime/js_engine.h + runtime/js_runtime.h only; fake
// engine is the existing FakeEngineHooks seam (in-process lambdas), no
// Hermes headers, no network, no env/config reads, no filesystem, no live
// services. Binds ONLY to the T6 `Interface:` line; never to implementation.
#include <cstdio>
#include <memory>
#include <string>

#include "runtime/js_engine.h"
#include "runtime/js_runtime.h"

namespace {

// In-process fake engine behind the FakeEngineHooks seam: counts evals,
// drains, collects and live handles.
struct FakeEngine {
  int evals = 0;
  int drains = 0;
  int collects = 0;
  int live_handles = 0;

  runtime::FakeEngineHooks hooks() {
    runtime::FakeEngineHooks h;
    h.eval = [this](const std::string& source, const std::string& origin) {
      ++evals;
      ++live_handles;
      (void)source;
      (void)origin;
      return runtime::JsValue{true, ""};
    };
    h.drain = [this]() { ++drains; };
    h.collect = [this]() { ++collects; };
    return h;
  }
};

}  // namespace

// Named case: create bootstraps exactly one isolate and handle set.
static bool check_create_yields_one_live_isolate() {
  FakeEngine fake;
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(fake.hooks());
  if (!engine) return false;
  return engine->liveIsolates() == 1;
}

// Named case: destroy releases everything — liveIsolates and liveHandles
// both return 0 afterwards.
static bool check_destroy_releases_isolate_and_handles() {
  FakeEngine fake;
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(fake.hooks());
  engine->mount("function body() {}");
  if (engine->liveHandles() == 0) return false;
  engine->destroy();
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: double-destroy is a no-op — never crashes, counters stay 0.
static bool check_double_destroy_is_noop() {
  FakeEngine fake;
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(fake.hooks());
  engine->mount("function body() {}");
  engine->destroy();
  try {
    engine->destroy();
  } catch (...) {
    return false;
  }
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: destroy without any mount still leaves zero counters.
static bool check_destroy_without_mount_leaves_zero() {
  FakeEngine fake;
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(fake.hooks());
  engine->destroy();
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
}

// Named case: create/destroy/create cycle — a fresh engine bootstraps a
// new isolate after the previous one was destroyed.
static bool check_recreate_after_destroy_bootstraps_again() {
  FakeEngine fake;
  {
    std::unique_ptr<runtime::JsEngine> first =
        runtime::JsEngine::create(fake.hooks());
    if (first->liveIsolates() != 1) return false;
    first->destroy();
    if (first->liveIsolates() != 0) return false;
  }
  std::unique_ptr<runtime::JsEngine> second =
      runtime::JsEngine::create(fake.hooks());
  if (!second) return false;
  if (second->liveIsolates() != 1) return false;
  second->destroy();
  return second->liveIsolates() == 0 && second->liveHandles() == 0;
}

// Named case: engine is usable between create and destroy — mount evals
// exactly once through the injected hooks.
static bool check_mount_between_create_and_destroy_evals_once() {
  FakeEngine fake;
  std::unique_ptr<runtime::JsEngine> engine =
      runtime::JsEngine::create(fake.hooks());
  const int before = fake.evals;
  runtime::InstanceId id = engine->mount("function body() {}");
  if (id == 0) return false;
  if (fake.evals != before + 1) return false;
  if (engine->bodyRunCount(id) != 1) return false;
  engine->destroy();
  return engine->liveIsolates() == 0 && engine->liveHandles() == 0;
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
  if (failures > 0) {
    std::printf("js_lifecycle_test: %d failing\n", failures);
    return 1;
  }
  std::printf("js_lifecycle_test: all passing\n");
  return 0;
}
