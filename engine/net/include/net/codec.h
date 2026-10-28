#pragma once

// Codec registry + validation + codegen pipeline surface
// (FR-NET-005 / FR-NET-009 / FR-NET-010; AC-NET-020 validation half,
// AC-NET-018; RULE-NET-004).
//
// Canonical-JSON in, wire bytes out: every Codec encodes canonical JSON
// text and decodes wire bytes back to canonical JSON text carried in
// DecodeResult::payload. Malformed input yields DecodeResult{ok:false}
// with a machine-readable `error` code and the offending `field` path —
// never a crash or an exception escape. CodecRegistry ships with all five
// codecs installed (install() replaces one entry); Validate() enforces a
// caller size cap before decoding. Typed bindings for new schemas are
// emitted by tools/net_codegen/codegen.py.

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace muse {
namespace net {

// Wire-format selector. kFlatBuffers is served by the FlatBuffers framing
// below (kept dependency-free like the other four); the registry serves
// all five uniformly.
enum class CodecId { kJson, kProtobuf, kFlatBuffers, kCbor, kMsgPack };

// Structured decode outcome. On failure `ok` is false, `error` is a
// non-empty machine code (e.g. "truncated", "size-cap-exceeded"), and
// `field` names the offending JSON path (possibly empty when no single
// field applies, e.g. truncation). Never throws.
struct DecodeResult {
  bool ok = false;
  std::string payload;
  std::string error;
  std::string field;
};

// Codec boundary: encode takes canonical JSON text and returns the wire
// bytes as std::string; decode inverts it back to canonical JSON text in
// `payload`. The never-throw promise covers encode/decode/Validate only:
// encode of malformed input returns "" ("invalid input, nothing emitted";
// note this is indistinguishable from encoding the empty string) and
// decode/Validate report malformed input as DecodeResult{ok:false}, never
// by throwing. Registry lookup is NOT part of the never-throw surface
// (see CodecRegistry::get).
class Codec {
 public:
  virtual ~Codec() = default;
  virtual std::string encode(const std::string& json) = 0;
  virtual DecodeResult decode(std::string_view bytes) = 0;
};

// Registry of the five production codecs. Default-constructed it serves
// every CodecId; install() replaces the entry for `id`. get() throws
// std::out_of_range on an out-of-range id or an empty (uninstalled) slot;
// callers that cannot prove the id valid must catch or pre-validate.
class CodecRegistry {
 public:
  CodecRegistry();
  ~CodecRegistry();
  void install(CodecId id, std::unique_ptr<Codec> c);
  Codec& get(CodecId id);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// JSON-text seam: keeps the canonicalizer dependency-free and swappable.
// The default backend parses strict JSON (no comments, no trailing
// commas, finite numbers only).
class IJsonBackend {
 public:
  virtual ~IJsonBackend() = default;
  // Canonicalize `in` into `out` (object keys sorted, minimal escapes).
  // Returns true on success; on failure sets `error` (machine code) and
  // `field` (offending path, possibly empty).
  virtual bool Canonicalize(std::string_view in, std::string* out,
                            std::string* error, std::string* field) = 0;
};

std::unique_ptr<IJsonBackend> DefaultJsonBackend();

// Validate wire `bytes` for codec `id` against a `maxBytes` size cap,
// then decode. Oversized or malformed input → DecodeResult{ok:false},
// never a throw.
DecodeResult Validate(CodecId id, std::string_view bytes, size_t maxBytes);

namespace codec_detail {

// Shared limits (validation half of AC-NET-018): inputs above the hard cap
// are rejected before any buffer is interpreted; nesting above the depth
// cap is rejected before recursing further.
inline constexpr size_t kMaxJsonBytes = 4u << 20;  // 4 MiB hard cap
inline constexpr size_t kMaxDepth = 100;           // nesting cap

DecodeResult Fail(std::string error, std::string field = "");
bool CanonicalizeJson(std::string_view in, std::string* out,
                      std::string* error, std::string* field);
std::string Frame(CodecId id, const std::string& canonical);
bool Unframe(CodecId id, std::string_view bytes, std::string_view* body,
             std::string* error);

}  // namespace codec_detail

// Production codecs (one TU each; FlatBuffers framing lives in codec.cc
// next to the registry since the plan keeps the existing FlatBuffers
// runtime instead of a new TU).
class JsonCodec : public Codec {
 public:
  std::string encode(const std::string& json) override;
  DecodeResult decode(std::string_view bytes) override;
};

class ProtobufCodec : public Codec {
 public:
  std::string encode(const std::string& json) override;
  DecodeResult decode(std::string_view bytes) override;
};

class FlatBuffersCodec : public Codec {
 public:
  std::string encode(const std::string& json) override;
  DecodeResult decode(std::string_view bytes) override;
};

class CborCodec : public Codec {
 public:
  std::string encode(const std::string& json) override;
  DecodeResult decode(std::string_view bytes) override;
};

class MsgPackCodec : public Codec {
 public:
  std::string encode(const std::string& json) override;
  DecodeResult decode(std::string_view bytes) override;
};

}  // namespace net
}  // namespace muse
