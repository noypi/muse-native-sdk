#include "layout/property.h"

#include <cstring>

namespace layout {

namespace {

// Float payloads move through the table as 32-bit patterns so a
// concurrent `get` observes the old or the new value, never a torn one.
std::uint32_t floatBits(float v) noexcept {
  static_assert(sizeof(float) == sizeof(std::uint32_t),
                "float payload must be 32 bits");
  std::uint32_t bits = 0;
  std::memcpy(&bits, &v, sizeof bits);
  return bits;
}

float bitsFloat(std::uint32_t bits) noexcept {
  float v = 0.0f;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

}  // namespace

PropertyTree::PropertyTree() : nodes_(new NodeSlot[kNodeSlots]) {
  // `new[]` value-initializes the atomics; pin the sentinels explicitly so
  // the table state never depends on atomic default-init rules.
  for (std::size_t n = 0; n < kNodeSlots; ++n) {
    nodes_[n].id.store(kInvalidNodeId, std::memory_order_relaxed);
    for (std::size_t i = 0; i < kMaxPropsPerNode; ++i) {
      nodes_[n].props[i].handle.store(kEmptyPropHandle,
                                      std::memory_order_relaxed);
      nodes_[n].props[i].bits.store(0, std::memory_order_relaxed);
    }
  }
}

PropertyTree::~PropertyTree() = default;

// Claims (or finds) the node slot for `node` via lock-free linear probing.
// Returns null when the table is full. `get` uses `findNode` instead so a
// lookup never mutates the table.
PropertyTree::NodeSlot* PropertyTree::findOrClaimNode(NodeSlot* base,
                                                      std::size_t count,
                                                      NodeId node) noexcept {
  const std::size_t home = static_cast<std::size_t>(node) % count;
  for (std::size_t i = 0; i < count; ++i) {
    NodeSlot* s = &base[(home + i) % count];
    NodeId cur = s->id.load(std::memory_order_acquire);
    if (cur == node) return s;
    if (cur == kInvalidNodeId) {
      NodeId expected = kInvalidNodeId;
      if (s->id.compare_exchange_strong(expected, node,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire))
        return s;
      if (expected == node) return s;
      // Claimed by another node; keep probing.
    }
  }
  return nullptr;
}

const PropertyTree::NodeSlot* PropertyTree::findNode(const NodeSlot* base,
                                                     std::size_t count,
                                                     NodeId node) noexcept {
  const std::size_t home = static_cast<std::size_t>(node) % count;
  for (std::size_t i = 0; i < count; ++i) {
    const NodeSlot* s = &base[(home + i) % count];
    const NodeId cur = s->id.load(std::memory_order_acquire);
    if (cur == node) return s;
    if (cur == kInvalidNodeId) return nullptr;
  }
  return nullptr;
}

void PropertyTree::set(NodeId node, PropHandle h, float value) noexcept {
  if (nodes_ == nullptr || node == kInvalidNodeId || h == kEmptyPropHandle)
    return;
  NodeSlot* slot = findOrClaimNode(nodes_.get(), kNodeSlots, node);
  if (slot == nullptr) return;  // Node table full: drop, never allocate.
  const std::uint32_t bits = floatBits(value);
  const std::size_t home = static_cast<std::size_t>(h % kMaxPropsPerNode);
  for (std::size_t i = 0; i < kMaxPropsPerNode; ++i) {
    PropSlot& p = slot->props[(home + i) % kMaxPropsPerNode];
    PropHandle cur = p.handle.load(std::memory_order_acquire);
    if (cur == h) {
      p.bits.store(bits, std::memory_order_release);
      return;
    }
    if (cur == kEmptyPropHandle) {
      PropHandle expected = kEmptyPropHandle;
      if (p.handle.compare_exchange_strong(expected, h,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
        p.bits.store(bits, std::memory_order_release);
        return;
      }
      if (expected == h) {
        // Lost the claim race to the same handle: adopt the winner slot.
        p.bits.store(bits, std::memory_order_release);
        return;
      }
      // Claimed by a different handle; keep probing.
    }
  }
  // Property table full: deterministically replace the home slot's payload
  // (bounded, allocation-free) rather than growing.
  slot->props[home].bits.store(bits, std::memory_order_release);
}

bool PropertyTree::get(NodeId node, PropHandle h, float& out) const
    noexcept {
  if (nodes_ == nullptr || node == kInvalidNodeId || h == kEmptyPropHandle)
    return false;
  const NodeSlot* slot = findNode(nodes_.get(), kNodeSlots, node);
  if (slot == nullptr) return false;
  const std::size_t home = static_cast<std::size_t>(h % kMaxPropsPerNode);
  for (std::size_t i = 0; i < kMaxPropsPerNode; ++i) {
    const PropSlot& p = slot->props[(home + i) % kMaxPropsPerNode];
    const PropHandle cur = p.handle.load(std::memory_order_acquire);
    if (cur == h) {
      out = bitsFloat(p.bits.load(std::memory_order_acquire));
      return true;
    }
    if (cur == kEmptyPropHandle) return false;
  }
  return false;
}

}  // namespace layout
