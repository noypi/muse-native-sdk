#pragma once

#include <cstddef>

#include "muse/state/atom.hpp"
#include "muse/state/descriptor.hpp"

namespace muse {
namespace state {

// W^X sandbox for server-driven descriptors (FR-STATE-009, AC-STATE-038).
// Static opcode allowlist audit: returns Status::kOk only when the descriptor
// contains zero native-library/code-execution opcodes; any "loadLibrary" or
// "execCode" entry is rejected with Status::kCorrupt, even if signed.
//
// The audit runs over the Descriptor model from muse/state/descriptor.hpp
// (node names are the audited opcodes). Matching is exact and case-sensitive;
// there is no substring or case-fold matching. The check is a pure function
// of the descriptor: it takes no registry or signature input, so a signature
// can never override a denial. An empty descriptor has zero native opcodes
// and passes. Never throws.
class Sandbox {
 public:
  Sandbox() = delete;

  static Status check(const Descriptor& d) noexcept;
};

}  // namespace state
}  // namespace muse
