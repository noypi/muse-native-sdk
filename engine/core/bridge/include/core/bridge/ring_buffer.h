#pragma once

// Pre-allocated ring buffer (FR-CORE-004 / AC-CORE-013; TC-CORE-013).
//
// Fixed-capacity ring modeled on muse::net::SharedRing
// (engine/net/include/net/zero_copy.h): tokens are 0-based insertion
// indices, overfill evicts the oldest records, and capacity counts
// PAYLOAD bytes only (envelope and container overhead excluded).
//
// place() COPIES the payload bytes once into ring-owned storage and
// returns a view of that copy — the no-allocation contract holds for the
// place/consume path after setup (mirroring muse::net::SharedRing):
// construction pre-allocates the byte store plus the record table, and
// place()/consume() perform no heap allocation afterwards. Views
// returned by place()/consume() are valid only until the record is
// evicted by wrap-around or the ring is destroyed; holding a view
// across further place() calls that overflow capacity (or across
// destruction) dangles. consume() of an evicted or never-placed token
// returns an empty view.
//
// NOTE: this header is intentionally self-contained (all methods
// defined inline). engine/core/bridge/CMakeLists.txt is test-wiring
// only and does not compile src/ring_buffer.cc into the `bridge`
// library, so out-of-line definitions would be unresolved at link
// time. src/ring_buffer.cc exists as the plan-owned translation unit
// and re-exports this header.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "core/bridge/envelope.h"

namespace muse {
namespace bridge {

class SharedRing {
 public:
  explicit SharedRing(size_t capacityBytes)
      : capacity_(capacityBytes == 0 ? 1 : capacityBytes),
        base_(0),
        head_(0),
        head_bytes_(0),
        used_(0),
        buf_(capacity_, '\0') {
    // Pre-allocate the record table at setup so place() never grows it
    // afterwards. One record per payload byte is the worst case for
    // non-empty payloads; empty-payload records consume no bytes but
    // still need slots, hence the floor.
    size_t slots = capacity_ < 1024 ? 1024 : capacity_;
    records_.reserve(slots);
  }

  ~SharedRing() = default;
  SharedRing(const SharedRing&) = delete;
  SharedRing& operator=(const SharedRing&) = delete;

  // Copies payload into ring-owned storage and returns a view of the
  // copy. Never throws: a payload larger than capacity (the
  // deterministic allocation-failure path) returns an empty view and
  // leaves the ring unchanged.
  std::string_view place(const Envelope& e, std::string_view payload) {
    // No heap allocation after setup: byte store and record table are
    // pre-allocated; every path below only memcpys/memmos and pushes
    // into reserved capacity. Any unexpected failure (including a
    // reserve-exhausted push) is contained and reported as empty.
    try {
      const size_t n = payload.size();
      if (n > capacity_) return std::string_view{};
      evict_for(n);
      if (head_bytes_ + used_ + n > capacity_) compact();
      if (n > 0) {
        std::memcpy(buf_.data() + head_bytes_ + used_, payload.data(), n);
      }
      records_.push_back(Record{head_bytes_ + used_, n, e});
      used_ += n;
      if (n == 0) return std::string_view{};
      const Record& stored = records_.back();
      return std::string_view(buf_.data() + stored.offset, stored.len);
    } catch (...) {
      return std::string_view{};
    }
  }

  std::string_view consume(uint64_t token) noexcept {
    return consume_impl(token);
  }
  std::string_view consume(uint64_t token) const noexcept {
    return consume_impl(token);
  }

 private:
  struct Record {
    size_t offset;
    size_t len;
    Envelope envelope;
  };

  size_t capacity_;
  uint64_t base_;    // insertion index (token) of records_[head_]
  size_t head_;      // index of the oldest live record in records_
  size_t head_bytes_;  // byte offset of the oldest live record in buf_
  size_t used_;      // live payload bytes retained
  std::vector<char> buf_;
  std::vector<Record> records_;

  size_t live() const noexcept { return records_.size() - head_; }

  void evict_oldest() noexcept {
    const Record& front = records_[head_];
    head_bytes_ += front.len;
    used_ -= front.len;
    ++head_;
    ++base_;
  }

  // Evicts oldest records until n new bytes fit and the record table
  // has a free reserved slot (so push_back cannot reallocate).
  // Always retains at least the incoming record itself.
  void evict_for(size_t n) noexcept {
    while (live() > 0 &&
           (used_ + n > capacity_ || live() == records_.capacity())) {
      evict_oldest();
    }
    // Fully-evicted ring restarts at offset zero; keeps offsets small.
    if (live() == 0) {
      head_ = 0;
      head_bytes_ = 0;
      used_ = 0;
      records_.clear();
    }
  }

  // Slides the live byte window back to offset zero; adjusts record
  // offsets. No allocation, never throws.
  void compact() noexcept {
    if (head_bytes_ == 0) return;
    if (used_ > 0) {
      std::memmove(buf_.data(), buf_.data() + head_bytes_, used_);
    }
    for (size_t i = head_; i < records_.size(); ++i) {
      records_[i].offset -= head_bytes_;
    }
    head_bytes_ = 0;
  }

  std::string_view consume_impl(uint64_t token) const noexcept {
    if (token < base_) return std::string_view{};  // evicted by wrap-around
    const uint64_t index = token - base_;
    if (index >= static_cast<uint64_t>(live())) {
      return std::string_view{};  // never placed
    }
    const Record& rec = records_[head_ + static_cast<size_t>(index)];
    // payloadLen beyond what is stored means corruption: reject, never
    // read out of bounds.
    if (static_cast<uint64_t>(rec.envelope.payloadLen) >
        static_cast<uint64_t>(rec.len)) {
      return std::string_view{};
    }
    if (rec.envelope.payloadLen == 0) return std::string_view{};
    return std::string_view(buf_.data() + rec.offset,
                            rec.envelope.payloadLen);
  }
};

}  // namespace bridge
}  // namespace muse
