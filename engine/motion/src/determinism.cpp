#include "motion/determinism.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "motion/spring.h"

namespace {

// Seeded RNG stream state (splitmix64). INTENTIONAL STUB: the
// hash/serialize/solver path is pure and never consults this state, so
// reseeding with the same seed trivially reproduces the identical observable
// sequence; the stream exists so stochastic extensions draw deterministically
// from the seeded value instead of any device or clock source. Tester-layer
// wiring point: a future motion_random* draw API reads/advances this state;
// until then motion_seed is write-only by design.
uint64_t g_rng_state = 0;

uint64_t HashBits(uint64_t bits, uint64_t hash) {
  // FNV-1a 64 over the 8 raw bytes, LSB first from the integer value, so
  // the hash depends only on the bit pattern — never on host endianness
  // or double semantics. Same fold as motion_emitFrame's stateHash.
  for (int i = 0; i < 8; ++i) {
    hash ^= (bits >> (i * 8)) & 0xFFu;
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint64_t HashDouble(double d, uint64_t hash) {
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
  uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  return HashBits(bits, hash);
}

void EncodeDoubleLE(double d, uint8_t* dst) {
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
  uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  for (int i = 0; i < 8; ++i) {
    dst[i] = static_cast<uint8_t>((bits >> (i * 8)) & 0xFFu);
  }
}

double DecodeDoubleLE(const uint8_t* src) {
  uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) {
    bits |= static_cast<uint64_t>(src[i]) << (i * 8);
  }
  double d = 0.0;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

}  // namespace

void motion_seed(uint64_t seed) noexcept { g_rng_state = seed; }

uint64_t motion_stateHash(const SpringState& s) noexcept {
  uint64_t hash = 14695981039346656037ULL;
  hash = HashDouble(s.x, hash);
  hash = HashDouble(s.v, hash);
  hash = HashDouble(s.stiffness, hash);
  hash = HashDouble(s.damping, hash);
  return hash;
}

std::vector<uint8_t> motion_serialize(const SpringState& s) noexcept {
  // Declaration order (x, v, stiffness, damping), 8 bytes LE each.
  std::vector<uint8_t> out(32, 0);
  EncodeDoubleLE(s.x, &out[0]);
  EncodeDoubleLE(s.v, &out[8]);
  EncodeDoubleLE(s.stiffness, &out[16]);
  EncodeDoubleLE(s.damping, &out[24]);
  return out;
}

bool motion_deserialize(const std::vector<uint8_t>& b, SpringState* out) noexcept {
  // Strict canonical form: exactly 32 bytes, non-null destination.
  // No throwing or trapping operations below: size check, null check,
  // then shift/or/memcpy only. Any 32-byte payload (including 0xFF
  // garbage or NaN bit patterns) decodes to some state.
  if (out == nullptr) return false;
  if (b.size() != 32u) return false;
  const uint8_t* p = b.data();
  SpringState s;
  s.x = DecodeDoubleLE(p);
  s.v = DecodeDoubleLE(p + 8);
  s.stiffness = DecodeDoubleLE(p + 16);
  s.damping = DecodeDoubleLE(p + 24);
  *out = s;
  return true;
}
