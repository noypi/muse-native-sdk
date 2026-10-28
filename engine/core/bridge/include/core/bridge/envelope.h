#pragma once

// Bridge envelope (FR-CORE-004 / AC-CORE-011; TC-CORE-011).
//
// Fixed-magic, little-endian header carrying schema id (u32),
// schema version (u16), ABI version (u16), and payload length (u32).
// DecodeEnvelope validates magic, header length, and
// payloadLen <= maximum mappable payload (64 KiB) before touching
// payload — it never reads payload bytes at all, so no out-of-bounds
// read is possible. Error codes are
// exactly "truncated" | "bad-magic" | "length-overflow".

#include <cstdint>
#include <string>
#include <string_view>

namespace muse {
namespace bridge {

struct Envelope {
  uint32_t schemaId;
  uint16_t schemaVersion;
  uint16_t abiVersion;
  uint32_t payloadLen = 0;
};

struct DecodeOutcome {
  bool ok;
  Envelope envelope;
  std::string error;
};

struct MismatchOutcome {
  bool ok;
  std::string error;
};

// Serializes the envelope header. The returned bytes hold the header
// only; the payload itself lives in the caller's mapping and its
// length is carried by Envelope::payloadLen.
std::string EncodeEnvelope(const Envelope& e);

// Parses bytes produced by EncodeEnvelope. Never throws, never reads
// past the supplied view.
DecodeOutcome DecodeEnvelope(std::string_view bytes) noexcept;

// Rejects schema/ABI mismatches before any buffer is interpreted.
// Compares only the envelope header fields (schema id, then schema
// version, then ABI version — same reject-before-interpret order as
// EnvelopeMatches in engine/net/src/zero_copy.cc) and never touches
// payload bytes: there is no payload parameter. Exact match returns
// ok=true with an empty error; a mismatch returns ok=false with
// exactly "schema-id-mismatch" | "schema-version-mismatch" |
// "abi-mismatch". Never throws.
MismatchOutcome CheckEnvelope(const Envelope& e, uint32_t expectedSchema,
                              uint16_t expectedSchemaVersion,
                              uint16_t expectedAbi) noexcept;

}  // namespace bridge
}  // namespace muse
