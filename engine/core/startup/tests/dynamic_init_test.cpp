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
// touches the network. The violation case writes a marker-bearing `.o`
// fixture into a build-dir-owned `test-fixtures/` path (created at runtime,
// marker assembled from fragments so this test's own object stays clean)
// and asserts the nonzero return plus the production `DYNAMIC_INIT:` stdout
// line; the `object_dir` edge calls use a nonexistent path (expecting
// nonzero fail-closed) plus the self-evident clean/dirty contract
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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unistd.h>

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
  // Exercises the production entry point for real: a fixture `.o` file
  // carrying a marker byte sequence must make check_dynamic_initializers
  // return nonzero, and the captured production stdout must carry a
  // `DYNAMIC_INIT:<file>:<symbol>`-shaped line.
  //
  // The marker is assembled at runtime from fragments (mirroring the
  // scanner's own fragment trick) so this test's object file never
  // contains a contiguous marker byte sequence.
  const std::string marker = std::string("_GLOBAL__") + "sub_I_";
  const char* dir = "test-fixtures/dynamic-init-dirty";
  const std::string obj = std::string(dir) + "/dirty.o";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  CHECK(!ec);
  {
    std::ofstream out(obj, std::ios::binary | std::ios::trunc);
    CHECK(static_cast<bool>(out));
    if (!out) return false;
    const std::string payload =
        std::string("ELFDATA-HEAD:") + marker + ":tail";
    out.write(payload.data(),
              static_cast<std::streamsize>(payload.size()));
    out.close();
    CHECK(static_cast<bool>(out));
  }
  // Capture the production stdout line via an fd-level redirect.
  const std::string cap = std::string(dir) + "/stdout.txt";
  std::fflush(stdout);
  const int saved = dup(STDOUT_FILENO);
  CHECK(saved >= 0);
  FILE* sink = std::fopen(cap.c_str(), "w");
  CHECK(sink != nullptr);
  if (sink == nullptr) {
    if (saved >= 0) close(saved);
    return false;
  }
  CHECK(dup2(fileno(sink), STDOUT_FILENO) >= 0);
  const int rc = check_dynamic_initializers(dir);
  std::fflush(stdout);
  CHECK(dup2(saved, STDOUT_FILENO) >= 0);
  close(saved);
  std::fclose(sink);
  CHECK(rc != 0);
  std::string output;
  {
    std::ifstream in(cap, std::ios::binary);
    CHECK(static_cast<bool>(in));
    if (in) {
      output.assign(std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
    }
  }
  CHECK(output.find("DYNAMIC_INIT:") != std::string::npos);
  CHECK(output.find("dirty.o") != std::string::npos);
  CHECK(output.find(marker) != std::string::npos);
  CHECK(output.find("DYNAMIC_INIT:dirty.o:" + marker) !=
        std::string::npos);
  // Intentional scope: the same marker in a non-object (`.txt`) sidecar
  // is correctly ignored, matching the tools/check_dynamic_init.py gate.
  {
    const char* txtDir = "test-fixtures/dynamic-init-txt-only";
    std::filesystem::create_directories(txtDir, ec);
    CHECK(!ec);
    const std::string sidecar = std::string(txtDir) + "/notes.txt";
    std::ofstream txt(sidecar, std::ios::binary | std::ios::trunc);
    CHECK(static_cast<bool>(txt));
    if (!txt) return false;
    const std::string payload =
        std::string("sidecar:") + marker + ":tail";
    txt.write(payload.data(),
              static_cast<std::streamsize>(payload.size()));
    txt.close();
    CHECK(static_cast<bool>(txt));
    CHECK(check_dynamic_initializers(txtDir) == 0);
  }
  return g_failed_checks == 0;
}

bool check_truncated_scan_returns_nonzero_with_truncated_line() {
  // Security pin (AC-CORE-031 fail-closed; security-review LOW truncation
  // finding): a >8 MB object whose only marker sits beyond the 8 MB scan
  // window must NOT return clean. Production returns `2` plus a
  // `DYNAMIC_INIT_TRUNCATED:<path>:over-cap` line on stderr — never `0`.
  // The marker is assembled at runtime from fragments so this test's own
  // object file never contains a contiguous marker byte sequence.
  const std::string marker = std::string("_GLOBAL__") + "sub_I_";
  const char* dir = "test-fixtures/dynamic-init-truncated";
  const std::string obj = std::string(dir) + "/big.o";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  CHECK(!ec);
  {
    std::ofstream out(obj, std::ios::binary | std::ios::trunc);
    CHECK(static_cast<bool>(out));
    if (!out) return false;
    // Fill past the 8 MB scan window before emitting the marker, so the
    // marker is reachable only by a full-file scan (which the capped
    // reader refuses to do).
    const std::size_t kFill = 8u * 1024u * 1024u + 4096u;
    const std::string chunk(65536, 'P');
    std::size_t written = 0;
    while (written < kFill) {
      const std::size_t n =
          (kFill - written < chunk.size()) ? (kFill - written) : chunk.size();
      out.write(chunk.data(), static_cast<std::streamsize>(n));
      written += n;
    }
    const std::string tail = marker + ":tail";
    out.write(tail.data(), static_cast<std::streamsize>(tail.size()));
    out.close();
    CHECK(static_cast<bool>(out));
  }
  // Capture the production stderr line via an fd-level redirect.
  const std::string cap = std::string(dir) + "/stderr.txt";
  std::fflush(stderr);
  const int saved = dup(STDERR_FILENO);
  CHECK(saved >= 0);
  FILE* sink = std::fopen(cap.c_str(), "w");
  CHECK(sink != nullptr);
  if (sink == nullptr) {
    if (saved >= 0) close(saved);
    return false;
  }
  CHECK(dup2(fileno(sink), STDERR_FILENO) >= 0);
  const int rc = check_dynamic_initializers(dir);
  std::fflush(stderr);
  CHECK(dup2(saved, STDERR_FILENO) >= 0);
  close(saved);
  std::fclose(sink);
  // Fail-closed: nonzero (exactly 2 = unscannable input), never 0.
  CHECK(rc != 0);
  CHECK(rc == 2);
  std::string errOutput;
  {
    std::ifstream in(cap, std::ios::binary);
    CHECK(static_cast<bool>(in));
    if (in) {
      errOutput.assign(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
    }
  }
  CHECK(errOutput.find("DYNAMIC_INIT_TRUNCATED:") != std::string::npos);
  CHECK(errOutput.find("big.o") != std::string::npos);
  CHECK(errOutput.find(":over-cap") != std::string::npos);
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
  RUN_CASE(check_truncated_scan_returns_nonzero_with_truncated_line);
  RUN_CASE(check_return_contract_values);
  if (failures > 0) {
    std::printf("dynamic_init_test: %d failing\n", failures);
    return 1;
  }
  std::printf("dynamic_init_test: all passing\n");
  return 0;
}
