#pragma once

// In-place payload consume (FR-CORE-004 / AC-CORE-014; TC-CORE-014).
//
// Views the first e.payloadLen bytes of an existing mapping without
// copying, mirroring muse::net::ConsumeInPlace
// (engine/net/src/zero_copy.cc). Envelope identity (schema id, schema
// version, ABI) is gated by CheckEnvelope before any buffer is
// interpreted; ConsumeInPlace enforces the length gate only: a declared
// payloadLen beyond what the mapping holds (including the degenerate
// empty mapping) yields an empty view instead of an out-of-bounds read.
// Never throws, never reads OOB, performs zero heap allocation.
//
// The returned view aliases the caller's mapping: data() lies inside
// [mapped.data(), mapped.data() + mapped.size()) with size payloadLen
// and identical bytes. It is valid only while the mapping is live;
// holding it across unmapping (or across ring eviction when the bytes
// came from SharedRing) dangles.

#include <string_view>

#include "core/bridge/envelope.h"

namespace muse {
namespace bridge {

// View the first e.payloadLen bytes of an existing mapping without
// copying. Returns an empty view when payloadLen exceeds the mapping
// (callers reject schema/ABI mismatches via CheckEnvelope first).
// Never throws, never reads OOB, performs no heap allocation.
std::string_view ConsumeInPlace(const Envelope& e,
                                std::string_view mapped) noexcept;

}  // namespace bridge
}  // namespace muse
