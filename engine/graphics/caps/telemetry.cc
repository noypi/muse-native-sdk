#include "caps/caps_tier.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace graphics {

namespace {

// Canonical serialization: every field contributes, length-prefixed so
// {"ab","c"} and {"a","bc"} cannot alias. Uses explicit '\0' separators plus
// length prefixes for the same reason.
void hash_mix(uint64_t& h, const char* data, uint64_t len) {
  static constexpr uint64_t kPrime = 1099511628211ULL;
  for (uint64_t i = 0; i < len; ++i) {
    h ^= static_cast<uint64_t>(static_cast<unsigned char>(data[i]));
    h *= kPrime;
  }
}

void hash_string(uint64_t& h, const std::string& s) {
  char len_buf[32];
  const int n =
      std::snprintf(len_buf, sizeof(len_buf), "%llu:", static_cast<unsigned long long>(s.size()));
  hash_mix(h, len_buf, static_cast<uint64_t>(n));
  if (!s.empty()) {
    hash_mix(h, s.data(), static_cast<uint64_t>(s.size()));
  }
  const char sep = '\0';
  hash_mix(h, &sep, 1);
}

uint64_t fingerprint_fnv1a(const DeviceFingerprint& fp, uint64_t seed) {
  uint64_t h = seed;
  hash_string(h, fp.vendor);
  hash_string(h, fp.gpu_id);
  hash_string(h, fp.driver_version);
  char ram_buf[32];
  const int n = std::snprintf(ram_buf, sizeof(ram_buf), "%d", fp.ram_gb);
  hash_mix(h, ram_buf, static_cast<uint64_t>(n));
  const char sep = '\0';
  hash_mix(h, &sep, 1);
  const unsigned char flags[3] = {
      static_cast<unsigned char>(fp.vk13_dynamic_rendering ? 1 : 0),
      static_cast<unsigned char>(fp.vk110_available ? 1 : 0),
      static_cast<unsigned char>(fp.gles32_available ? 1 : 0),
  };
  hash_mix(h, reinterpret_cast<const char*>(flags), 3);
  return h;
}

}  // namespace

// Stable 128-bit fingerprint hash rendered as 32 lowercase hex chars.
// Two FNV-1a/64 passes with distinct offsets over the same canonical bytes:
// deterministic across runs, fixed length, and carrying no raw fingerprint
// material (opaque digest only).
std::string fingerprint_hash_for(const DeviceFingerprint& fingerprint) {
  static constexpr uint64_t kOffsetA = 14695981039346656037ULL;
  static constexpr uint64_t kOffsetB = 1099511628211ULL ^ 0x9e3779b97f4a7c15ULL;
  const uint64_t hi = fingerprint_fnv1a(fingerprint, kOffsetA);
  const uint64_t lo = fingerprint_fnv1a(fingerprint, kOffsetB);
  char out[33];
  std::snprintf(out, sizeof(out), "%016llx%016llx",
                static_cast<unsigned long long>(hi),
                static_cast<unsigned long long>(lo));
  return std::string(out, 32);
}

}  // namespace graphics
