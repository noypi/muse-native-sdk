#pragma once

// Zero-copy payload surface (FR-NET-010 / AC-NET-019; RULE-NET-004;
// FR-CORE-004 consumed envelope + ring shape only).
//
// The "shared memory" under test is process memory managed by SharedRing
// (fd passing / JSI handle table are core-owned and out of scope).
// consume returns a view INTO the original mapping: zero copies beyond
// the initial map. Version mismatch yields an empty view (typed-error
// signal), never an OOB read, never a throw.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace muse {
namespace net {

struct Envelope {
  uint32_t schemaId;
  uint16_t schemaVersion;
  uint16_t abiVersion;
  uint32_t payloadLen;
};

// The single schema/ABI identity this build consumes in place.
// Mismatches are rejected before any buffer is interpreted.
inline constexpr uint32_t kExpectedSchemaId = 1;
inline constexpr uint16_t kExpectedSchemaVersion = 1;
inline constexpr uint16_t kExpectedAbiVersion = 1;

// View the first e.payloadLen bytes of an existing mapping without
// copying. Returns an empty view when the envelope identity does not
// match or payloadLen exceeds the mapping. Never throws, never reads
// OOB, performs no heap allocation.
//
// SharedRing storage model (test double, not real shm accounting):
// place() COPIES the payload into ring-owned storage and returns a view
// of that copy — "zero-copy" holds only for the consume path (views into
// the original mapping / retained storage, zero copies beyond the initial
// map/place). Views returned by place()/consume() are valid only until
// the record is evicted by wrap-around or the ring is destroyed; holding
// a view across further place() calls that overflow capacity (or across
// destruction) dangles. consume() of an evicted or never-placed token
// returns an empty view. capacity counts PAYLOAD bytes only (envelope and
// std::string overhead excluded).
std::string_view ConsumeInPlace(const Envelope& e,
                                std::string_view mapped) noexcept;

// Pre-allocated ring of process-shared payload bytes. Tokens are 0-based
// insertion indices: consume(n) retrieves the n-th placed record.
// Overfilling evicts the oldest records; the newest remain consumable
// in order. consume() performs no heap allocation and never throws.
class SharedRing {
 public:
  explicit SharedRing(size_t bytes);
  ~SharedRing();
  SharedRing(const SharedRing&) = delete;
  SharedRing& operator=(const SharedRing&) = delete;

  std::string_view place(const Envelope& e, std::string_view payload);
  std::string_view consume(uint64_t token) noexcept;
  std::string_view consume(uint64_t token) const noexcept;

 private:
  size_t capacity_;
  uint64_t base_;  // insertion index of front record
  size_t used_;    // bytes currently retained
  std::deque<Envelope> envelopes_;
  std::deque<std::string> payloads_;

  std::string_view consume_impl(uint64_t token) const noexcept;
};

}  // namespace net
}  // namespace muse
