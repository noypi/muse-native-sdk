#include "net/zero_copy.h"

namespace muse {
namespace net {

namespace {

// Identity check shared by both consume paths: reject before any
// buffer is interpreted.
bool EnvelopeMatches(const Envelope& e) noexcept {
  return e.schemaId == kExpectedSchemaId &&
         e.schemaVersion == kExpectedSchemaVersion &&
         e.abiVersion == kExpectedAbiVersion;
}

}  // namespace

std::string_view ConsumeInPlace(const Envelope& e,
                                std::string_view mapped) noexcept {
  if (!EnvelopeMatches(e)) return std::string_view{};
  // payloadLen is uint32_t; compare in 64-bit to avoid any truncation.
  if (static_cast<uint64_t>(e.payloadLen) >
      static_cast<uint64_t>(mapped.size())) {
    return std::string_view{};
  }
  // Sub-span of the caller's mapping: no copy, no allocation.
  return std::string_view(mapped.data(), e.payloadLen);
}

SharedRing::SharedRing(size_t bytes)
    : capacity_(bytes == 0 ? 1 : bytes), base_(0), used_(0) {}

SharedRing::~SharedRing() = default;

std::string_view SharedRing::place(const Envelope& e,
                                   std::string_view payload) {
  // Evict oldest records until the new payload fits (ring wrap-around).
  // Always retain at least the incoming record itself. Allocation failure
  // yields the documented empty-view signal instead of escaping.
  try {
    while (!payloads_.empty() && used_ + payload.size() > capacity_) {
      used_ -= payloads_.front().size();
      payloads_.pop_front();
      envelopes_.pop_front();
      ++base_;
    }
    envelopes_.push_back(e);
    payloads_.emplace_back(payload.data(), payload.size());
  } catch (...) {
    // Roll back a half-pushed envelope so the two deques stay paired.
    while (envelopes_.size() > payloads_.size()) envelopes_.pop_back();
    return std::string_view{};
  }
  used_ += payloads_.back().size();
  const std::string& stored = payloads_.back();
  return std::string_view(stored.data(), stored.size());
}

std::string_view SharedRing::consume_impl(uint64_t token) const noexcept {
  if (token < base_) return std::string_view{};  // evicted by wrap-around
  uint64_t index = token - base_;
  if (index >= static_cast<uint64_t>(payloads_.size())) {
    return std::string_view{};  // never placed
  }
  size_t i = static_cast<size_t>(index);
  const Envelope& e = envelopes_[i];
  if (!EnvelopeMatches(e)) return std::string_view{};
  const std::string& stored = payloads_[i];
  // payloadLen beyond what is stored means corruption: reject, never OOB.
  if (static_cast<uint64_t>(e.payloadLen) >
      static_cast<uint64_t>(stored.size())) {
    return std::string_view{};
  }
  return std::string_view(stored.data(), e.payloadLen);
}

std::string_view SharedRing::consume(uint64_t token) noexcept {
  return consume_impl(token);
}

std::string_view SharedRing::consume(uint64_t token) const noexcept {
  return consume_impl(token);
}

}  // namespace net
}  // namespace muse
