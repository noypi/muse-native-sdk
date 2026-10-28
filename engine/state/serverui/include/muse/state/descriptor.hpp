#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "muse/state/atom.hpp"

namespace muse {
namespace state {

// Newer-than-client gate (assumption A2): descriptors carrying a version
// greater than this fail soft with Status::kCorrupt (bundled fallback),
// never a crash.
inline constexpr std::uint64_t kClientVersion = 2;

// Trust seam for descriptor verification (SEC-INTEGRITY consumed via seam;
// tests bind a fake verifier key, never the real Keystore).
struct TrustAnchor {
  const char* keyId;
  std::uint64_t validUntil;
};

// Default-constructible, equality-comparable property bag (structural subset).
// Owned here so the canonical Descriptor node surface can carry Props without
// a circular include; serverui.hpp reuses this type (no redefinition).
class Props {
 public:
  Props() = default;

  bool operator==(const Props& /*other*/) const { return true; }
  bool operator!=(const Props& /*other*/) const { return false; }
};

// Opcode surface for the W^X sandbox audit (T10). Exact, case-sensitive
// names; "loadLibrary"/"execCode" are denied by Sandbox::check.
struct Op {
  std::string name;
  Op() = default;
  explicit Op(const char* n) : name(n == nullptr ? "" : n) {}
  explicit Op(std::string n) : name(std::move(n)) {}
};

// Unified canonical Descriptor (T1.10 production unification, batch 4 T8+T9+T10).
// Sole owner of `muse::state::Descriptor`: envelope (version/keyId/
// validUntil/payload/signature) + node surface (addNode/nodeCount/nodeName/
// nodeProps) + op surface (addOp/opCount/opName). serverui.hpp and sandbox.hpp
// include this header and MUST NOT redefine Descriptor.
class Descriptor {
 public:
  Descriptor() = default;
  explicit Descriptor(std::uint64_t version) : version_(version) {}

  // Envelope accessors (verify pieces intact).
  std::uint64_t version() const { return version_; }
  const std::string& keyId() const { return key_id_; }
  std::uint64_t validUntil() const { return valid_until_; }
  const std::string& payload() const { return payload_; }
  const std::vector<std::uint8_t>& signature() const { return signature_; }

  // Node surface (T8 component registry).
  void addNode(const char* name, const Props& props) {
    nodes_.emplace_back(name == nullptr ? "" : name, props);
  }
  // Compatibility overload for envelope payload strings (verify.cpp path):
  // stores the name with default Props.
  void addNode(const char* name, const std::string& /*payload*/) {
    nodes_.emplace_back(name == nullptr ? "" : name, Props());
  }

  std::size_t nodeCount() const { return nodes_.size(); }

  const char* nodeName(std::size_t index) const {
    if (index >= nodes_.size()) return nullptr;
    return nodes_[index].first.c_str();
  }

  const Props& nodeProps(std::size_t index) const {
    return nodes_.at(index).second;
  }

  // Op surface (T10 sandbox audit).
  void addOp(Op o) { ops_.push_back(std::move(o)); }
  void addOp(const char* opcode) {
    ops_.emplace_back(opcode == nullptr ? "" : opcode);
  }
  void addOp(const std::string& opcode) { ops_.emplace_back(opcode); }

  std::size_t opCount() const { return ops_.size(); }

  const char* opName(std::size_t index) const {
    if (index >= ops_.size()) return nullptr;
    return ops_[index].name.c_str();
  }

  const Op& opAt(std::size_t index) const { return ops_.at(index); }

 private:
  std::uint64_t version_ = 0;
  std::string key_id_;
  std::uint64_t valid_until_ = 0;
  std::string payload_;
  std::vector<std::uint8_t> signature_;
  std::vector<std::pair<std::string, Props>> nodes_;
  std::vector<Op> ops_;
};

// Raw descriptor bytes as received from the server (signed envelope or
// plain/unsigned payload).
class DescriptorBytes {
 public:
  DescriptorBytes() = default;

  static DescriptorBytes from(const char* data, std::size_t size) {
    DescriptorBytes b;
    if (data != nullptr && size > 0) {
      b.data_.assign(data, size);
    }
    return b;
  }

  bool empty() const { return data_.empty(); }
  const std::string& data() const { return data_; }

 private:
  friend DescriptorBytes signTestOnly(const char*, const char*,
                                      std::uint64_t, std::uint64_t);
  explicit DescriptorBytes(std::string data) : data_(std::move(data)) {}
  std::string data_;
};

// Fake verifier clock for tests (hermetic; never touches the OS clock).
// Default 0 (no expiry); tests set a fake now via setTestOnlyNow.
inline std::uint64_t& testOnlyNowStorage() {
  static std::uint64_t now = 0;
  return now;
}

inline void setTestOnlyNow(std::uint64_t now) { testOnlyNowStorage() = now; }

// FNV-1a (64-bit) checksum used as the fake signature envelope. Never real
// crypto; production verifies against the TrustAnchor seam.
inline std::uint64_t fakeSigHash(const char* key_id, std::uint64_t valid_until,
                                 std::uint64_t version, const char* payload,
                                 std::size_t payload_size) {
  std::uint64_t h = 1469598103934665603ULL;
  auto mix = [&h](unsigned char c) {
    h ^= static_cast<std::uint64_t>(c);
    h *= 1099511628211ULL;
  };
  for (const char* p = key_id; p != nullptr && *p != '\0'; ++p) {
    mix(static_cast<unsigned char>(*p));
  }
  for (int i = 0; i < 8; ++i) {
    mix(static_cast<unsigned char>((valid_until >> (i * 8)) & 0xFFU));
  }
  for (int i = 0; i < 8; ++i) {
    mix(static_cast<unsigned char>((version >> (i * 8)) & 0xFFU));
  }
  for (std::size_t i = 0; i < payload_size; ++i) {
    mix(static_cast<unsigned char>(payload[i]));
  }
  return h;
}

// Test-only signer: wraps the payload in a fake-signature envelope keyed by
// (keyId, validUntil, version). Production never calls this; tests use the
// dummy key "test-key-1", never a real Keystore.
inline DescriptorBytes signTestOnly(const char* payload, const char* key_id,
                                    std::uint64_t valid_until,
                                    std::uint64_t version) {
  const std::string body = payload == nullptr ? "" : payload;
  const std::string key = key_id == nullptr ? "" : key_id;
  const std::uint64_t sig =
      fakeSigHash(key.c_str(), valid_until, version, body.data(), body.size());
  char sig_hex[17];
  std::snprintf(sig_hex, sizeof(sig_hex), "%016llx",
                static_cast<unsigned long long>(sig));
  std::string out("MUSE1|");
  out += std::to_string(version);
  out += '|';
  out += key;
  out += '|';
  out += std::to_string(valid_until);
  out += '|';
  out += std::to_string(body.size());
  out += '|';
  out += body;
  out += "|#";
  out += sig_hex;
  return DescriptorBytes(out);
}

// Verifies a signed descriptor envelope against the trust anchor.
// unsigned / mis-signed / expired / malformed / newer-than-client (A2) all
// return Status::kCorrupt (caller uses the bundled screen). Never throws.
StatusOr<Descriptor> verifyDescriptor(const DescriptorBytes& b,
                                      const TrustAnchor& a);

}  // namespace state
}  // namespace muse
