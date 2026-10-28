#include "core/bridge/envelope.h"

namespace muse {
namespace bridge {

namespace {

// Fixed magic identifying bridge envelope headers (coder's choice per
// T1 contract A-WIRE; tests pin rejection, not exact bytes).
inline constexpr char kMagic[4] = {'M', 'B', 'D', 'G'};

// Header layout: magic[4] | schemaId u32 LE | schemaVersion u16 LE |
// abiVersion u16 LE | payloadLen u32 LE.
inline constexpr size_t kHeaderSize = 4 + 4 + 2 + 2 + 4;

// Maximum mappable payload (plan T1 acceptance: 1..64KiB payloads).
// DecodeEnvelope carries only the header — the payload itself lives in
// the caller's mapping — so a declared length beyond what any mapping
// can hold is rejected as "length-overflow" without touching payload.
inline constexpr uint32_t kMaxPayloadLen = 64u * 1024u;

void PutU32LE(std::string& out, uint32_t v) {
  out.push_back(static_cast<char>(v & 0xFFu));
  out.push_back(static_cast<char>((v >> 8) & 0xFFu));
  out.push_back(static_cast<char>((v >> 16) & 0xFFu));
  out.push_back(static_cast<char>((v >> 24) & 0xFFu));
}

void PutU16LE(std::string& out, uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFFu));
  out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

uint32_t GetU32LE(const char* p) noexcept {
  return static_cast<uint32_t>(static_cast<unsigned char>(p[0])) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[1])) << 8) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[2])) << 16) |
         (static_cast<uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

uint16_t GetU16LE(const char* p) noexcept {
  return static_cast<uint16_t>(static_cast<unsigned char>(p[0])) |
         (static_cast<uint16_t>(static_cast<unsigned char>(p[1])) << 8);
}

}  // namespace

std::string EncodeEnvelope(const Envelope& e) {
  std::string out;
  out.reserve(kHeaderSize);
  out.append(kMagic, 4);
  PutU32LE(out, e.schemaId);
  PutU16LE(out, e.schemaVersion);
  PutU16LE(out, e.abiVersion);
  PutU32LE(out, e.payloadLen);
  return out;
}

DecodeOutcome DecodeEnvelope(std::string_view bytes) noexcept {
  DecodeOutcome out{false, Envelope{}, ""};
  try {
    if (bytes.size() < kHeaderSize) {
      out.error = "truncated";
      return out;
    }
    const char* p = bytes.data();
    if (p[0] != kMagic[0] || p[1] != kMagic[1] || p[2] != kMagic[2] ||
        p[3] != kMagic[3]) {
      out.error = "bad-magic";
      return out;
    }
    Envelope e;
    e.schemaId = GetU32LE(p + 4);
    e.schemaVersion = GetU16LE(p + 8);
    e.abiVersion = GetU16LE(p + 10);
    e.payloadLen = GetU32LE(p + 12);
    // The view holds the header; the payload itself lives in the
    // caller's mapping (EncodeEnvelope emits the header only), so the
    // declared length is validated against the maximum mappable payload
    // before any payload is touched — Decode never reads payload bytes
    // at all, so no out-of-bounds read is possible. payloadLen is u32:
    // compare in 64-bit to avoid any truncation.
    if (static_cast<uint64_t>(e.payloadLen) >
        static_cast<uint64_t>(kMaxPayloadLen)) {
      out.error = "length-overflow";
      return out;
    }
    out.ok = true;
    out.envelope = e;
    out.error.clear();
  } catch (...) {
    // None-throw surface: allocation failure degrades to "truncated".
    out.ok = false;
    out.envelope = Envelope{};
    try {
      out.error = "truncated";
    } catch (...) {
    }
  }
  return out;
}

MismatchOutcome CheckEnvelope(const Envelope& e, uint32_t expectedSchema,
                              uint16_t expectedSchemaVersion,
                              uint16_t expectedAbi) noexcept {
  MismatchOutcome out{false, ""};
  try {
    // Field order pins multi-field precedence (contract A-PRECEDENCE):
    // schema id first, then schema version, then ABI.
    if (e.schemaId != expectedSchema) {
      out.ok = false;
      out.error = "schema-id-mismatch";
      return out;
    }
    if (e.schemaVersion != expectedSchemaVersion) {
      out.ok = false;
      out.error = "schema-version-mismatch";
      return out;
    }
    if (e.abiVersion != expectedAbi) {
      out.ok = false;
      out.error = "abi-mismatch";
      return out;
    }
    out.ok = true;
    out.error.clear();
  } catch (...) {
    // None-throw surface: allocation failure degrades to a typed error.
    // Schema-id code preserves the A-PRECEDENCE order.
    out.ok = false;
    try {
      out.error = "schema-id-mismatch";
    } catch (...) {
    }
  }
  return out;
}

}  // namespace bridge
}  // namespace muse
