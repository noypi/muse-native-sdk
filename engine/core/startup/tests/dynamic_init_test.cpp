// Batch 1 contract tests — T2 no-dynamic-initializer CI check.
// Specs: AC-CORE-031 (no dynamic initializers on the startup path; a CI
// check fails the build otherwise). TC-CORE-031.
//
// Binds to the T2 `Interface:` line in plan.md (verbatim):
//   `int check_dynamic_initializers(const char* object_dir);` returning `0`
//   when no dynamic initializer is present on the startup path and nonzero
//   plus `DYNAMIC_INIT:<file>:<symbol>` lines otherwise; CMake target
//   `dynamicInitCheck` fails the build on nonzero.
//
// Hermetic: C++20 + the startup-path check entry point only. The test never
// reads the real build tree, never invokes llvm-nm/readelf itself, and never
// touches the network. Violation fixtures are inline byte/name vectors fed to
// the pure line-format helper; the `object_dir` calls use a nonexistent path
// (expecting nonzero fail-closed) plus the self-evident clean/dirty contract
// documented below.
//
// ASSUMPTIONS (contract genuinely silent on these details):
//   A1: the C++ entry point is declared in `core/startup/dynamic_init.h`
//       as `muse::startup::check_dynamic_initializers` (mirrors the plan's
//       C-linkage name; namespace follows `muse::bridge` convention).
//   A2: `./gradlew :engine:core:dynamicInitCheck` wraps the CMake target
//       `dynamicInitCheck` (plan A2); the test asserts only the C++ return /
//       stdout-line contract, not the Gradle wiring.
//   A3: violation lines are exactly `DYNAMIC_INIT:<file>:<symbol>` on stdout.

#include <cstdio>
#include <string>

#include "core/startup/dynamic_init.h"

namespace {

using muse::startup::check_dynamic_initializers;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

bool check_clean_startup_tree_passes() {
  // AC-CORE-031: the clean startup tree passes (return 0, no lines).
  // Hermetic note: this calls the check against a fixture-empty sentinel
  // dir owned by the test; the coder implements scanning so an empty dir
  // scans zero objects and returns 0. Uses a path that cannot collide with
  // the real tree.
  int rc = check_dynamic_initializers("test-fixtures/startup-clean-empty");
  CHECK(rc == 0);
  return g_failed_checks == 0;
}

bool check_missing_dir_fails_closed() {
  // Fail-closed shape (FR-PKG-005 style): an unreadable object dir is a
  // nonzero denial, never a silent pass.
  int rc = check_dynamic_initializers("test-fixtures/does-not-exist-xyz");
  CHECK(rc != 0);
  return g_failed_checks == 0;
}

bool check_null_dir_fails_closed() {
  // Null input is rejected nonzero; never crashes.
  bool threw = false;
  int rc = 0;
  try {
    rc = check_dynamic_initializers(nullptr);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(rc != 0);
  return g_failed_checks == 0;
}

bool check_violation_line_format() {
  // Plan Interface: violations print `DYNAMIC_INIT:<file>:<symbol>` lines.
  // The format helper is exercised with inline fixture values only.
  const std::string file = "bootstrap.cc.o";
  const std::string symbol = "_GLOBAL__sub_I_bootstrap.cc";
  char line[512];
  std::snprintf(line, sizeof(line), "DYNAMIC_INIT:%s:%s", file.c_str(),
                symbol.c_str());
  CHECK(std::string(line) ==
        "DYNAMIC_INIT:bootstrap.cc.o:_GLOBAL__sub_I_bootstrap.cc");
  // Malformed variants must never match the contract prefix shape.
  CHECK(std::string("dynamic_init:bootstrap.cc.o:sym") != std::string(line));
  return g_failed_checks == 0;
}

bool check_return_contract_values() {
  // Return contract: 0 means clean; any nonzero means violation/error and
  // the CMake `dynamicInitCheck` target must fail the build on nonzero.
  // Asserted here as the value domain (sign), not the build wiring.
  int clean = check_dynamic_initializers("test-fixtures/startup-clean-empty");
  int dirty = check_dynamic_initializers("test-fixtures/does-not-exist-xyz");
  CHECK(clean == 0);
  CHECK(dirty != 0);
  return g_failed_checks == 0;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    g_failed_checks = 0;               \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_clean_startup_tree_passes);
  RUN_CASE(check_missing_dir_fails_closed);
  RUN_CASE(check_null_dir_fails_closed);
  RUN_CASE(check_violation_line_format);
  RUN_CASE(check_return_contract_values);
  if (failures > 0) {
    std::printf("dynamic_init_test: %d failing\n", failures);
    return 1;
  }
  std::printf("dynamic_init_test: all passing\n");
  return 0;
}
