#include "muse/state/descriptor.hpp"

namespace muse {
namespace state {
namespace {

// Parses an unsigned decimal; returns false on any non-digit or overflow.
bool parseU64(const std::string& s, std::size_t begin, std::size_t end,
              std::uint64_t* out) {
  if (begin >= end || out == nullptr) return false;
  std::uint64_t v = 0;
  for (std::size_t i = begin; i < end; ++i) {
    const char c = s[i];
    if (c < '0' || c > '9') return false;
    const std::uint64_t d = static_cast<std::uint64_t>(c - '0');
    if (v > (UINT64_MAX - d) / 10U) return false;
    v = v * 10U + d;
  }
  *out = v;
  return true;
}

bool parseHex16(const std::string& s, std::size_t begin, std::uint64_t* out) {
  if (s.size() - begin != 16 || out == nullptr) return false;
  std::uint64_t v = 0;
  for (std::size_t i = begin; i < s.size(); ++i) {
    const char c = s[i];
    std::uint64_t d = 0;
    if (c >= '0' && c <= '9') {
      d = static_cast<std::uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      d = static_cast<std::uint64_t>(c - 'a') + 10U;
    } else {
      return false;
    }
    v = (v << 4U) | d;
  }
  *out = v;
  return true;
}

}  // namespace

StatusOr<Descriptor> verifyDescriptor(const DescriptorBytes& b,
                                      const TrustAnchor& a) {
  // Null anchor key or empty input can never verify; fail soft, no throw.
  if (a.keyId == nullptr || b.empty()) return Status::kCorrupt;

  const std::string& s = b.data();
  constexpr char kPrefix[] = "MUSE1|";
  constexpr std::size_t kPrefixLen = 6;
  if (s.size() < kPrefixLen + 4 || s.compare(0, kPrefixLen, kPrefix) != 0) {
    return Status::kCorrupt;  // unsigned (plain payload), not an envelope.
  }

  std::size_t pos = kPrefixLen;
  const std::size_t ver_end = s.find('|', pos);
  const std::size_t key_end =
      ver_end == std::string::npos ? std::string::npos : s.find('|', ver_end + 1);
  const std::size_t exp_end =
      key_end == std::string::npos ? std::string::npos : s.find('|', key_end + 1);
  const std::size_t len_end =
      exp_end == std::string::npos ? std::string::npos : s.find('|', exp_end + 1);
  if (ver_end == std::string::npos || key_end == std::string::npos ||
      exp_end == std::string::npos || len_end == std::string::npos) {
    return Status::kCorrupt;
  }

  std::uint64_t version = 0;
  std::uint64_t valid_until = 0;
  std::uint64_t payload_len = 0;
  if (!parseU64(s, pos, ver_end, &version)) return Status::kCorrupt;
  // validUntil sits between key_end+1 and exp_end; payload length between
  // exp_end+1 and len_end.
  if (!parseU64(s, key_end + 1, exp_end, &valid_until)) {
    return Status::kCorrupt;
  }
  if (!parseU64(s, exp_end + 1, len_end, &payload_len)) {
    return Status::kCorrupt;
  }

  const std::string key_id = s.substr(ver_end + 1, key_end - ver_end - 1);
  if (key_id.empty()) return Status::kCorrupt;

  const std::size_t body_begin = len_end + 1;
  // Need exactly: body + "|#" + 16 hex digits.
  if (s.size() < body_begin + payload_len + 2 + 16) return Status::kCorrupt;
  if (s[body_begin + payload_len] != '|' ||
      s[body_begin + payload_len + 1] != '#') {
    return Status::kCorrupt;
  }
  if (s.size() != body_begin + payload_len + 2 + 16) return Status::kCorrupt;

  std::uint64_t sig = 0;
  if (!parseHex16(s, body_begin + payload_len + 2, &sig)) {
    return Status::kCorrupt;
  }

  const char* body_ptr = s.data() + body_begin;
  const std::uint64_t expect =
      fakeSigHash(key_id.c_str(), valid_until, version, body_ptr, payload_len);
  if (expect != sig) return Status::kCorrupt;  // tampered / mis-signed.

  // Key allowlist: envelope key must match the anchor key.
  if (key_id != a.keyId) return Status::kCorrupt;

  // Expiry: both the envelope and the anchor must still be valid.
  const std::uint64_t now = testOnlyNowStorage();
  if (valid_until < now || a.validUntil < now) return Status::kCorrupt;

  // A2: newer-than-client fails soft to bundled, never crash.
  if (version > kClientVersion) return Status::kCorrupt;

  Descriptor d(version);
  const std::string payload(body_ptr, payload_len);
  d.addNode(payload.c_str(), payload);
  return d;
}

}  // namespace state
}  // namespace muse
