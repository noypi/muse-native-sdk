// T5 contract tests — NFR-CORE-010 / AC-CORE-023 (TC-CORE-023).
// Main-thread checker: JNI Java-UI-touching calls must run on main;
// off-main call trips debug assert / violation flag naming the call site.
//
// Binds ONLY to the T5 `Interface:` line; never to implementation.
// Hermetic: C++17 + planned headers <core/thread/main_checker.h> only;
// no network, no env/config reads, no filesystem, no live services, no JNI.
//
// Expected state: RED — <core/thread/main_checker.h> does not exist yet,
// so this file fails to compile until the coder implements T5.
#include <cstdio>
#include <string>
#include <thread>

#include "core/thread/main_checker.h"

// --- tiny harness (matches engine/runtime/tests convention) ----------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// --- cases ----------------------------------------------------------------

// On-main check passes and records no violation.
static void check_on_main_check_passes() {
  core::thread::MainThreadChecker::resetForTest();
  core::thread::MainThreadChecker::setMainThreadId(std::this_thread::get_id());
  core::thread::MainThreadChecker::check("java_ui_call");
  CHECK("on-main check records no violation",
        core::thread::MainThreadChecker::violations() == 0);
}

// Off-main check records a violation (release) / trips debug assert.
static void check_off_main_check_records_violation() {
  core::thread::MainThreadChecker::resetForTest();
  core::thread::MainThreadChecker::setMainThreadId(std::this_thread::get_id());
  std::thread worker([] {
    core::thread::MainThreadChecker::check("off_main_java_ui_call");
  });
  worker.join();
  CHECK("off-main check records a violation",
        core::thread::MainThreadChecker::violations() == 1);
}

// Violation flag names the call site (what string is observable).
static void check_violation_names_call_site() {
  core::thread::MainThreadChecker::resetForTest();
  core::thread::MainThreadChecker::setMainThreadId(std::this_thread::get_id());
  std::thread worker([] {
    core::thread::MainThreadChecker::check("TextView.setText");
  });
  worker.join();
  CHECK("one violation recorded",
        core::thread::MainThreadChecker::violations() == 1);
  CHECK("violation names the call site",
        std::string(core::thread::MainThreadChecker::lastWhat()) ==
            "TextView.setText");
}

// Host seam: main-thread identity is injectable, so tests are hermetic.
static void check_main_thread_id_is_injectable() {
  core::thread::MainThreadChecker::resetForTest();
  const std::thread::id fake = std::thread::id();
  core::thread::MainThreadChecker::setMainThreadId(fake);
  // Current thread is not the injected id -> violation expected.
  core::thread::MainThreadChecker::check("seam_probe");
  CHECK("injected id differs from this thread trips violation",
        core::thread::MainThreadChecker::violations() == 1);
  core::thread::MainThreadChecker::resetForTest();
  core::thread::MainThreadChecker::setMainThreadId(std::this_thread::get_id());
  core::thread::MainThreadChecker::check("seam_probe");
  CHECK("re-injected this-thread id passes",
        core::thread::MainThreadChecker::violations() == 0);
}

// LocalRefScope RAII: depth counts push/pop, balanced on scope exit.
static void check_local_ref_scope_depth_counts() {
  CHECK("depth starts at zero",
        core::thread::LocalRefScope::depth() == 0);
  {
    core::thread::LocalRefScope s1;
    CHECK("one live scope gives depth one",
          core::thread::LocalRefScope::depth() == 1);
    {
      core::thread::LocalRefScope s2;
      CHECK("nested scopes nest depth",
            core::thread::LocalRefScope::depth() == 2);
    }
    CHECK("inner scope exit pops depth",
          core::thread::LocalRefScope::depth() == 1);
  }
  CHECK("all scopes exited restores depth zero",
        core::thread::LocalRefScope::depth() == 0);
}

// Boundary: check(nullptr) must not crash; records a violation off-main,
// passes on-main.
static void check_null_what_safe() {
  core::thread::MainThreadChecker::resetForTest();
  core::thread::MainThreadChecker::setMainThreadId(std::this_thread::get_id());
  core::thread::MainThreadChecker::check(nullptr);
  CHECK("null what on main is safe and clean",
        core::thread::MainThreadChecker::violations() == 0);
}

int main() {
  check_on_main_check_passes();
  check_off_main_check_records_violation();
  check_violation_names_call_site();
  check_main_thread_id_is_injectable();
  check_local_ref_scope_depth_counts();
  check_null_what_safe();
  if (s_failures == 0) {
    std::printf("ALL GREEN main_thread_test\n");
    return 0;
  }
  std::printf("%d FAILURES main_thread_test\n", s_failures);
  return 1;
}
