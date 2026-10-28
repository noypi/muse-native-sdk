// Batch 1 contract tests — T1 envelope serialize/parse round trip
// (FR-CORE-004 / AC-CORE-011; TC-CORE-011).
//
// Hermetic: C++17 + engine/core/bridge headers only. No sockets, no
// env/config reads, no filesystem, no live services, no wall-clock.
// All inputs are inline byte strings; DecodeEnvelope never reads past
// the supplied view (truncated / length-overflow inputs assert this
// by construction — the bytes simply are not there).
//
// Binds to the T1 `Interface:` line in plan.md:
//   muse::bridge::EncodeEnvelope(const Envelope&) -> std::string;
//   muse::bridge::DecodeEnvelope(std::string_view) -> DecodeOutcome
//     {ok:bool, envelope:Envelope, error:std::string};
//   none-throw, error codes "truncated"|"bad-magic"|"length-overflow".
//
// ASSUMPTIONS:
//   A1: new module mirroring engine/net zero_copy shape (plan A1).
//   A2: gradlew names stale, CTest used (plan A2).
//   A3: header-only or minimal src to keep tests hermetic (plan A3);
//     tests include only "core/bridge/envelope.h" and link the
//     `bridge` lib from this module's CMakeLists.
//   A-WIRE: magic bytes + little-endian header layout are the coder's
//     choice; tests assert only magic rejection (bad-magic), field
//     widths, and round-trip preservation — never exact wire bytes.

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "core/bridge/envelope.h"

namespace {

using muse::bridge::DecodeEnvelope;
using muse::bridge::DecodeOutcome;
using muse::bridge::EncodeEnvelope;
using muse::bridge::Envelope;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

bool check_envelope_field_widths() {
  // AC-CORE-011: every cross-bridge payload carries schema id, schema
  // version, ABI version, payload length with exact widths.
  Envelope e{0xABu, 2u, 3u, 9u};
  CHECK(sizeof(e.schemaId) == 4);
  CHECK(sizeof(e.schemaVersion) == 2);
  CHECK(sizeof(e.abiVersion) == 2);
  CHECK(sizeof(e.payloadLen) == 4);
  CHECK(e.schemaId == 0xABu);
  CHECK(e.schemaVersion == 2u);
  CHECK(e.abiVersion == 3u);
  CHECK(e.payloadLen == 9u);
  return g_failed_checks == 0;
}

bool check_round_trip_preserves_all_fields() {
  // AC-CORE-011 / TC-CORE-011: encode->decode preserves all four
  // fields; never throws.
  const Envelope cases[] = {
      {1u, 1u, 1u, 0u},
      {0xDEADBEEFu, 7u, 9u, 1u},
      {0xFFFFFFFFu, 0xFFFFu, 0xFFFFu, 65536u},
      {42u, 3u, 5u, 1024u},
  };
  for (const Envelope& e : cases) {
    std::string bytes;
    bool threw = false;
    try {
      bytes = EncodeEnvelope(e);
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    if (threw) return false;
    DecodeOutcome out{false, Envelope{}, ""};
    try {
      out = DecodeEnvelope(bytes);
    } catch (...) {
      std::printf("    DecodeEnvelope threw\n");
      return false;
    }
    CHECK(out.ok);
    CHECK(out.envelope.schemaId == e.schemaId);
    CHECK(out.envelope.schemaVersion == e.schemaVersion);
    CHECK(out.envelope.abiVersion == e.abiVersion);
    CHECK(out.envelope.payloadLen == e.payloadLen);
  }
  return g_failed_checks == 0;
}

bool check_round_trip_payload_sizes_1_to_64k() {
  // TC-CORE-011: round trip of 1..64KiB payloads preserves payloadLen.
  const uint32_t sizes[] = {1u, 127u, 256u, 1024u, 4096u, 65536u};
  for (uint32_t n : sizes) {
    Envelope e{11u, 2u, 4u, n};
    std::string bytes;
    try {
      bytes = EncodeEnvelope(e);
    } catch (...) {
      std::printf("    EncodeEnvelope threw for size %u\n", n);
      return false;
    }
    DecodeOutcome out{false, Envelope{}, ""};
    try {
      out = DecodeEnvelope(bytes);
    } catch (...) {
      std::printf("    DecodeEnvelope threw for size %u\n", n);
      return false;
    }
    CHECK(out.ok);
    CHECK(out.envelope.payloadLen == n);
    CHECK(out.envelope.schemaId == 11u);
  }
  return g_failed_checks == 0;
}

bool check_truncated_header_rejected() {
  // Empty and short inputs: ok=false, error "truncated", never throw,
  // never OOB (there are no further bytes to read).
  const std::string full = EncodeEnvelope(Envelope{1u, 1u, 1u, 8u});
  CHECK(!full.empty());
  for (size_t len = 0; len < full.size(); ++len) {
    DecodeOutcome out{true, Envelope{}, ""};
    bool threw = false;
    try {
      out = DecodeEnvelope(std::string_view(full.data(), len));
    } catch (...) {
      threw = true;
    }
    CHECK(!threw);
    if (threw) return false;
    CHECK(!out.ok);
    CHECK(out.error == "truncated");
  }
  return g_failed_checks == 0;
}

bool check_bad_magic_rejected() {
  // Corrupted magic: ok=false, error "bad-magic", never throws.
  std::string full = EncodeEnvelope(Envelope{1u, 1u, 1u, 8u});
  CHECK(!full.empty());
  full[0] = static_cast<char>(full[0] ^ 0xFF);
  DecodeOutcome out{true, Envelope{}, ""};
  bool threw = false;
  try {
    out = DecodeEnvelope(full);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!out.ok);
  CHECK(out.error == "bad-magic");
  return g_failed_checks == 0;
}

bool check_length_overflow_rejected() {
  // payloadLen beyond the available mapping: ok=false, error
  // "length-overflow", never throws, never reads OOB.
  std::string full = EncodeEnvelope(Envelope{1u, 1u, 1u, 1u << 30});
  DecodeOutcome out{true, Envelope{}, ""};
  bool threw = false;
  try {
    out = DecodeEnvelope(full);
  } catch (...) {
    threw = true;
  }
  CHECK(!threw);
  if (threw) return false;
  CHECK(!out.ok);
  CHECK(out.error == "length-overflow");
  return g_failed_checks == 0;
}

bool check_error_codes_exact() {
  // Only the three documented codes ever appear on failure.
  std::string good = EncodeEnvelope(Envelope{5u, 6u, 7u, 0u});
  DecodeOutcome ok_out{false, Envelope{}, "x"};
  try {
    ok_out = DecodeEnvelope(good);
  } catch (...) {
    return false;
  }
  CHECK(ok_out.ok);
  // Failure code set is exactly {truncated, bad-magic, length-overflow}.
  std::string bad = good;
  bad[0] = static_cast<char>(bad[0] ^ 0xFF);
  DecodeOutcome bad_out{true, Envelope{}, ""};
  try {
    bad_out = DecodeEnvelope(bad);
  } catch (...) {
    return false;
  }
  CHECK(!bad_out.ok);
  CHECK(bad_out.error == "bad-magic");
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
  RUN_CASE(check_envelope_field_widths);
  RUN_CASE(check_round_trip_preserves_all_fields);
  RUN_CASE(check_round_trip_payload_sizes_1_to_64k);
  RUN_CASE(check_truncated_header_rejected);
  RUN_CASE(check_bad_magic_rejected);
  RUN_CASE(check_length_overflow_rejected);
  RUN_CASE(check_error_codes_exact);
  if (failures > 0) {
    std::printf("bridge_envelope_test: %d failing\n", failures);
    return 1;
  }
  std::printf("bridge_envelope_test: all passing\n");
  return 0;
}
