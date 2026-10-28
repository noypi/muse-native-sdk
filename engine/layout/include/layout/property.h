#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "layout/types.h"

namespace layout {

// Handle for a pre-hashed style property name (32-bit FNV-1a of the name).
// Call sites pass string literals through `prop_constexpr` so the hash is
// computed at compile time; the runtime `prop` below exists for
// dynamically-formed names. There is deliberately no string-keyed lookup
// API: no serialized style object exists on this path by construction.
using PropHandle = std::uint32_t;

// Fixed per-node property capacity. `PropertyTree::set` beyond this count
// on one node deterministically replaces the home slot instead of
// allocating; see the implementation for the documented choice.
inline constexpr std::size_t kMaxPropsPerNode = 64;

// 0 is the empty-handle sentinel (slot vacant marker). `prop(nullptr)`
// returns it; real names never hash to it (folded to 1).
inline constexpr PropHandle kEmptyPropHandle = 0;

namespace detail {

inline constexpr PropHandle kFnvOffsetBasis = 2166136261u;
inline constexpr PropHandle kFnvPrime = 16777619u;

// Shared FNV-1a core so the constexpr and runtime spellings agree bit for
// bit. A 0 digest is folded to 1 so 0 stays reserved for `kEmptyPropHandle`.
constexpr PropHandle fnv1a(const char* s) noexcept {
  PropHandle h = kFnvOffsetBasis;
  for (std::size_t i = 0; s[i] != '\0'; ++i) {
    h ^= static_cast<PropHandle>(static_cast<unsigned char>(s[i]));
    h *= kFnvPrime;
  }
  return h == kEmptyPropHandle ? 1u : h;
}

}  // namespace detail

// Compile-time hash for literals: `constexpr auto w = prop_constexpr("width");`
constexpr PropHandle prop_constexpr(const char* name) noexcept {
  return detail::fnv1a(name);
}

// Runtime FNV-1a over a NUL-terminated name. Never allocates, never throws;
// a null pointer yields `kEmptyPropHandle`.
inline PropHandle prop(const char* name) noexcept {
  if (name == nullptr) return kEmptyPropHandle;
  PropHandle h = detail::kFnvOffsetBasis;
  while (*name != '\0') {
    h ^= static_cast<PropHandle>(static_cast<unsigned char>(*name));
    h *= detail::kFnvPrime;
    ++name;
  }
  return h == kEmptyPropHandle ? 1u : h;
}

// Lock-free style property store keyed by pre-hashed handles.
//
// Storage: a fixed open-addressing node table (512 slots, allocated once
// in the constructor — never per mutation) where each node slot carries a
// fixed 64-entry open-addressing property table. Both levels are claimed
// with compare-exchange on atomics; there are no locks, no `new`/`malloc`
// on the mutation path, and `set`/`get` are `noexcept`.
//
// Concurrency: `set` on distinct nodes is data-race-free (disjoint slots
// after the claim; shared probe prefixes use acquire/release ordering).
// Concurrent `set`s on the *same* node and handle are safe but only one
// value wins; concurrent `set`/`get` on the same entry may observe the old
// or the new value, never a torn one (single 32-bit atomic payload).
//
// Documented limits: `kInvalidNodeId` is not a usable node id; at most 512
// distinct nodes and `kMaxPropsPerNode` properties per node are retained —
// a set past either capacity is dropped/replaced deterministically and
// never allocates. `get` of an unknown handle (or untouched node) returns
// `false` and leaves `out` untouched.
class PropertyTree {
 public:
  PropertyTree();
  ~PropertyTree();

  PropertyTree(const PropertyTree&) = delete;
  PropertyTree& operator=(const PropertyTree&) = delete;

  void set(NodeId node, PropHandle h, float value) noexcept;
  bool get(NodeId node, PropHandle h, float& out) const noexcept;

 private:
  struct PropSlot {
    std::atomic<PropHandle> handle{kEmptyPropHandle};
    std::atomic<std::uint32_t> bits{0};
  };
  struct NodeSlot {
    std::atomic<NodeId> id{kInvalidNodeId};
    PropSlot props[kMaxPropsPerNode];
  };
  static constexpr std::size_t kNodeSlots = 512;

  std::unique_ptr<NodeSlot[]> nodes_;

  static NodeSlot* findOrClaimNode(NodeSlot* base, std::size_t count,
                                   NodeId node) noexcept;
  static const NodeSlot* findNode(const NodeSlot* base, std::size_t count,
                                  NodeId node) noexcept;
};

}  // namespace layout
