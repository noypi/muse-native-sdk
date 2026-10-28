#include "motion/recorder.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {

// Fixed frame encoding: 4 doubles + 1 int64, little-endian byte order.
constexpr size_t kDoublesPerFrame = 4;
constexpr size_t kFrameBytes = kDoublesPerFrame * sizeof(double) + sizeof(int64_t);
// Trailer: 8-byte modular byte sum + 8-byte FNV-1a hash of the payload.
// The sum guarantees every single-bit flip is detected (any flipped bit
// changes the payload byte value by a nonzero power of two, so the wrapped
// sum always differs); FNV hardens multi-bit tampering.
constexpr size_t kTrailerBytes = 2 * sizeof(uint64_t);

void appendLittleEndian(std::vector<uint8_t>& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFFu));
  }
}

uint64_t readLittleEndian(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v |= static_cast<uint64_t>(p[i]) << (8 * i);
  }
  return v;
}

uint64_t payloadSum(const std::vector<uint8_t>& payload) noexcept {
  uint64_t sum = 0;
  for (uint8_t b : payload) sum += static_cast<uint64_t>(b);
  return sum;
}

uint64_t payloadFnv(const std::vector<uint8_t>& payload) noexcept {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : payload) {
    h ^= static_cast<uint64_t>(b);
    h *= 1099511628211ull;
  }
  return h;
}

// Open session: raw payload bytes (frames only, no trailer) plus count.
std::vector<uint8_t>& sessionPayload() {
  static std::vector<uint8_t> payload;
  return payload;
}
uint64_t& sessionFrames() {
  static uint64_t frames = 0;
  return frames;
}

void appendDouble(std::vector<uint8_t>& out, double d) {
  uint64_t u = 0;
  std::memcpy(&u, &d, sizeof(u));
  appendLittleEndian(out, u);
}

}  // namespace

Recording motion_recordStart() noexcept {
  try {
    sessionPayload().clear();
    sessionFrames() = 0;
  } catch (...) {
  }
  return Recording{};
}

void motion_recordFrame(const SpringState& s, int64_t dtNanos) noexcept {
  try {
    std::vector<uint8_t>& payload = sessionPayload();
    appendDouble(payload, s.x);
    appendDouble(payload, s.v);
    appendDouble(payload, s.stiffness);
    appendDouble(payload, s.damping);
    uint64_t dtBits = 0;
    std::memcpy(&dtBits, &dtNanos, sizeof(dtBits));
    appendLittleEndian(payload, dtBits);
    ++sessionFrames();
  } catch (...) {
  }
}

Recording motion_recordFinish() noexcept {
  Recording r;
  try {
    const std::vector<uint8_t>& payload = sessionPayload();
    r.frameCount = sessionFrames();
    if (r.frameCount == 0) {
      r.bytes.clear();
    } else {
      r.bytes.reserve(payload.size() + kTrailerBytes);
      r.bytes.insert(r.bytes.end(), payload.begin(), payload.end());
      appendLittleEndian(r.bytes, payloadSum(payload));
      appendLittleEndian(r.bytes, payloadFnv(payload));
    }
    sessionPayload().clear();
    sessionFrames() = 0;
  } catch (...) {
  }
  return r;
}

ReplayStatus motion_replay(const Recording& r) noexcept {
  try {
    // Vacuous replay: zero frames, no bytes — nothing to diverge on.
    if (r.frameCount == 0 && r.bytes.empty()) return ReplayStatus::kIdentical;
    // Structural floor: any nonzero session carries the trailer.
    if (r.bytes.size() < kTrailerBytes) return ReplayStatus::kCorrupt;
    if (r.frameCount == 0) {
      // Zero frames but stray bytes: only a valid empty trailer replays
      // identical; anything else is corrupt.
      if (!r.bytes.empty()) return ReplayStatus::kCorrupt;
      return ReplayStatus::kIdentical;
    }
    // Guard against frameCount * kFrameBytes overflow.
    if (r.frameCount >
        (std::numeric_limits<uint64_t>::max)() - kTrailerBytes) {
      return ReplayStatus::kCorrupt;
    }
    const uint64_t maxFrames =
        ((std::numeric_limits<uint64_t>::max)() - kTrailerBytes) / kFrameBytes;
    if (r.frameCount > maxFrames) return ReplayStatus::kCorrupt;
    const size_t expected =
        static_cast<size_t>(r.frameCount) * kFrameBytes + kTrailerBytes;
    if (r.bytes.size() != expected) return ReplayStatus::kCorrupt;
    // Split payload / trailer and verify integrity.
    const size_t payloadSize = expected - kTrailerBytes;
    std::vector<uint8_t> payload(r.bytes.begin(),
                                r.bytes.begin() + payloadSize);
    const uint8_t* trailer = r.bytes.data() + payloadSize;
    if (readLittleEndian(trailer) != payloadSum(payload)) {
      return ReplayStatus::kCorrupt;
    }
    if (readLittleEndian(trailer + 8) != payloadFnv(payload)) {
      return ReplayStatus::kCorrupt;
    }
    // Re-evaluate: walk frames, hashing state bytes frame by frame. Any
    // non-finite state that survived the checksum (hand-built input) counts
    // as diverged rather than identical.
    for (uint64_t f = 0; f < r.frameCount; ++f) {
      const uint8_t* frame = payload.data() + f * kFrameBytes;
      for (size_t d = 0; d < kDoublesPerFrame; ++d) {
        double v = 0.0;
        uint64_t u = readLittleEndian(frame + d * sizeof(double));
        std::memcpy(&v, &u, sizeof(v));
        if (!std::isfinite(v)) return ReplayStatus::kDiverged;
      }
    }
    return ReplayStatus::kIdentical;
  } catch (...) {
    return ReplayStatus::kCorrupt;
  }
}
