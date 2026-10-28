#ifndef MOTION_DETERMINISM_H_
#define MOTION_DETERMINISM_H_

// Deterministic replay support: seeded RNG boundary, stable state hashing,
// and canonical little-endian serialization
// (FR-MOTION-009 / AC-MOTION-029).
//
// The replay path never reads the wall clock: reseeding with the same seed
// reproduces the identical per-frame hash/serialization sequence, and
// scheduling jitter between runs changes nothing.
//
// Canonical encoding: the 4 doubles of SpringState in declaration order
// (x, v, stiffness, damping), each stored little-endian by value (LSB
// first from the IEEE-754 bit pattern), for exactly 32 bytes. The encoding
// is lossless and host-endianness independent: 1.0 always encodes as
// 00 00 00 00 00 00 F0 3F and a zero state as all-zero bytes.
//
// Error channel: motion_deserialize returns false on malformed input
// (wrong size, null `out`) and never throws, aborts, or hangs. Any
// 32-byte payload decodes to some state (raw bit patterns, including
// NaN/inf, round-trip losslessly); only the size/null checks can fail.

#include <cstdint>
#include <vector>

#include "motion/spring.h"

// Reseeds the motion RNG stream. Observable boundary: reseeding with the
// same seed reproduces the identical hash/serialization sequence.
// Never throws, never aborts.
void motion_seed(uint64_t seed) noexcept;

// Stable deterministic hash of the full spring state (x, v, stiffness,
// damping): same bits -> same hash (including NaN/inf/denormal), and the
// hash discriminates differing components. Bitwise FNV-1a 64 over the raw
// component bits, matching motion_emitFrame's stateHash. Pure: no global
// state is read. Never throws, never aborts.
uint64_t motion_stateHash(const SpringState& s) noexcept;

// Canonical LE serialization: exactly 32 bytes, deterministic, with equal
// states encoding byte-identically. Never aborts.
std::vector<uint8_t> motion_serialize(const SpringState& s) noexcept;

// Decodes a canonical 32-byte payload produced by motion_serialize.
// Returns false (leaving `*out` untouched) when `b.size() != 32` or
// `out == nullptr`. Never throws, never aborts.
bool motion_deserialize(const std::vector<uint8_t>& b, SpringState* out) noexcept;

#endif  // MOTION_DETERMINISM_H_
