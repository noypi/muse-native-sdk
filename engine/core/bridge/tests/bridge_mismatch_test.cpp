// Batch 1 contract tests — T2 schema/ABI mismatch rejection
// (FR-CORE-004 / AC-CORE-012; TC-CORE-012).
//
// Hermetic: C++17 + engine/core/bridge headers only. No sockets, no
// env/config reads, no filesystem, no live services, no wall-clock.
// CheckEnvelope takes only the envelope + expected identities —
// there is no payload parameter, so rejection provably happens
// before any buffer is interpreted. No payload bytes exist in this
// file at all.
//
// Binds to the T2 `Interface:` line in plan.md:
//   muse::bridge::CheckEnvelope(const Envelope&, uint32_t
//     expectedSchema, uint16_t expectedSchemaVersion, uint16_t
//     expectedAbi) -> MismatchOutcome {ok:bool, error:std::string};
//   error codes
//     "schema-id-mismatch"|"schema-version-mismatch"|"abi-mismatch";
//   never throws, never touches payload bytes.
//
// ASSUMPTIONS:
//   A1: new module mirroring engine/net zero_copy shape (plan A1).
//   A2: gradlew names stale, CTest used (plan A2).
//   A3: header-only or minimal src to keep tests hermetic (plan A3);
//     tests include only "core/bridge/envelope.h" and link the
//     `bridge` lib from this module's CMakeLists.
//   A-PRECEDENCE: when several fields mismatch at once, the reported
//     code follows field order — schema id, then schema version, then
//     ABI. The Interface line does not pin multi-field precedence;
//     single-field cases below are definitive, the all-wrong case
//     pins the most defensible order.

#include <cstdint>
#include <cstdio>

#include "core/bridge/envelope.h"

namespace {

using muse::bridge::CheckEnvelope;
using muse::bridge::Envelope;
using muse::bridge::MismatchOutcome;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

bool check_exact_match_passes() {
  // TC-CORE-012 control: exact match proceeds (ok=true, empty error).
  Envelope e{1u, 2u, 3u};
  MismatchOutcome out{false, "x"};
  bool threw = false;
  try {
    out = CheckEnvelope(e, 1u, 2u, 3u);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(out.ok);
  CHECK(out.error.empty());
  return g_failed_checks == 0;
}

bool check_wrong_schema_id_rejected() {
  // AC-CORE-012: wrong schemaId -> "schema-id-mismatch", never throws.
  Envelope e{2u, 2u, 3u};
  MismatchOutcome out{true, ""};
  bool threw = false;
  try {
    out = CheckEnvelope(e, 1u, 2u, 3u);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!out.ok);
  CHECK(out.error == "schema-id-mismatch");
  return g_failed_checks == 0;
}

bool check_wrong_schema_version_rejected() {
  // AC-CORE-012: wrong schemaVersion -> "schema-version-mismatch".
  Envelope e{1u, 99u, 3u};
  MismatchOutcome out{true, ""};
  bool threw = false;
  try {
    out = CheckEnvelope(e, 1u, 2u, 3u);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!out.ok);
  CHECK(out.error == "schema-version-mismatch");
  return g_failed_checks == 0;
}

bool check_wrong_abi_rejected() {
  // AC-CORE-012: wrong abiVersion -> "abi-mismatch".
  Envelope e{1u, 2u, 99u};
  MismatchOutcome out{true, ""};
  bool threw = false;
  try {
    out = CheckEnvelope(e, 1u, 2u, 3u);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!out.ok);
  CHECK(out.error == "abi-mismatch");
  return g_failed_checks == 0;
}

bool check_each_mismatch_is_independent() {
  // Each single-field mismatch yields exactly its own code; the other
  // two fields matching is what isolates the cause.
  struct Case {
    Envelope e;
    const char* want;
  };
  const Case cases[] = {
      {{7u, 2u, 3u}, "schema-id-mismatch"},
      {{1u, 8u, 3u}, "schema-version-mismatch"},
      {{1u, 2u, 9u}, "abi-mismatch"},
  };
  for (const Case& c : cases) {
    MismatchOutcome out{true, ""};
    bool threw = false;
    try {
      out = CheckEnvelope(c.e, 1u, 2u, 3u);
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    if (threw) return false;
    CHECK(!out.ok);
    CHECK(out.error == c.want);
  }
  return g_failed_checks == 0;
}

bool check_all_wrong_reports_schema_id_first() {
  // A-PRECEDENCE: all fields wrong -> schema-id code (field order).
  Envelope e{0u, 0u, 0u};
  MismatchOutcome out{true, ""};
  bool threw = false;
  try {
    out = CheckEnvelope(e, 1u, 2u, 3u);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!out.ok);
  CHECK(out.error == "schema-id-mismatch");
  return g_failed_checks == 0;
}

bool check_never_throws_on_extreme_values() {
  // Boundary values never throw: max/min identities on both sides.
  const Envelope envelopes[] = {
      {0u, 0u, 0u},
      {0xFFFFFFFFu, 0xFFFFu, 0xFFFFu},
      {1u, 1u, 1u},
  };
  for (const Envelope& e : envelopes) {
    bool threw = false;
    try {
      (void)CheckEnvelope(e, 0xFFFFFFFFu, 0xFFFFu, 0xFFFFu);
      (void)CheckEnvelope(e, 0u, 0u, 0u);
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    if (threw) return false;
  }
  // Max-vs-max is an exact match and must pass.
  MismatchOutcome out{false, "x"};
  try {
    out = CheckEnvelope(envelopes[1], 0xFFFFFFFFu, 0xFFFFu, 0xFFFFu);
  } catch (...) {
    std::printf("    CheckEnvelope threw on max match\n");
    return false;
  }
  CHECK(out.ok);
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
  RUN_CASE(check_exact_match_passes);
  RUN_CASE(check_wrong_schema_id_rejected);
  RUN_CASE(check_wrong_schema_version_rejected);
  RUN_CASE(check_wrong_abi_rejected);
  RUN_CASE(check_each_mismatch_is_independent);
  RUN_CASE(check_all_wrong_reports_schema_id_first);
  RUN_CASE(check_never_throws_on_extreme_values);
  if (failures > 0) {
    std::printf("bridge_mismatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("bridge_mismatch_test: all passing\n");
  return 0;
}
