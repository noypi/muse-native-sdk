#include "motion/trace.h"

#include <cstdint>
#include <cstring>

namespace {

uint64_t HashBits(uint64_t bits, uint64_t hash) noexcept {
  // FNV-1a 64 over the 8 raw bytes, little-endian order independent:
  // fold byte by byte from the integer value so the hash depends only on
  // the bit pattern, never on host endianness or double semantics.
  for (int i = 0; i < 8; ++i) {
    hash ^= static_cast<uint64_t>((bits >> (i * 8)) & 0xFFu);
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint64_t HashDouble(double d, uint64_t hash) noexcept {
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
  uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  return HashBits(bits, hash);
}

}  // namespace

TraceFrame motion_emitFrame(uint64_t frameId, int64_t nanos,
                            const SpringState& s) noexcept {
  // FNV-1a 64 offset basis; fold each state component's raw bits so the
  // hash is a pure deterministic function of the full state (same bits ->
  // same hash, including NaN/inf/denormal; differing x/v/stiffness/damping
  // -> differing hashes). Bitwise, branch-free: infallible over the whole
  // input range. memcpy only; no float arithmetic, no allocation.
  uint64_t hash = 14695981039346656037ULL;
  hash = HashDouble(s.x, hash);
  hash = HashDouble(s.v, hash);
  hash = HashDouble(s.stiffness, hash);
  hash = HashDouble(s.damping, hash);
  TraceFrame f;
  f.frameId = frameId;
  f.nanos = nanos;
  f.stateHash = hash;
  return f;
}
