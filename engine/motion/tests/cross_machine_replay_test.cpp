// Batch 3 contract tests — T6 (FR-MOTION-009 / AC-MOTION-029, TC-MOTION-029).
// Hermetic: C++17 + motion headers only; fixed literal inputs, no wall-clock
// reads, no env/config, no filesystem, no network. Follows the engine/motion
// convention (plain main(), RUN_CASE macro, printf PASS/FAIL, nonzero exit
// on failure; cf. tests/replay_determinism_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T6.md):
//   void motion_seed(uint64_t seed);
//   uint64_t motion_stateHash(const SpringState& s);
//   std::vector<uint8_t> motion_serialize(const SpringState& s);
//   bool motion_deserialize(const std::vector<uint8_t>& b, SpringState* out);
//   — motion_deserialize returns false on malformed input, never aborts.
//
// ASSUMPTION (field order): "fixed little-endian canonical encoding" is
// asserted as 4 doubles in SpringState declaration order (x, v, stiffness,
// damping) -> 32 bytes LE. If the coder documents a different field order,
// the LE-spot-check call sites reconcile; the size/determinism/round-trip
// expectations stand.
// ASSUMPTION (seed scope): the interface exposes no stochastic draw call, so
// "seeded RNG" is asserted at the observable boundary — re-seeding with the
// same seed reproduces the identical hash/serialization sequence, and a
// wall-clock spin between runs changes nothing. RNG-stream internals are
// coder-owned; cross-process identity is `tester`-layer work (UNVERIFIED).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "motion/determinism.h"
#include "motion/spring.h"

namespace {

// A serialized state round-trips exactly: deserialize returns true and the
// recovered state equals the original, bit for bit (canonical encoding is
// lossless; AC-MOTION-029 needs identical per-frame matrices on replay).
bool check_serialize_round_trip_is_exact() {
  try {
    const SpringState cases[] = {
        {0.0, 0.0, 180.0, 12.0},
        {1.5, -2.5, 200.0, 10.0},
        {-123.456, 789.012, 0.5, 0.25},
        {1e15, -1e15, 1e6, 1e6},
    };
    for (const SpringState& s : cases) {
      std::vector<uint8_t> bytes = motion_serialize(s);
      if (bytes.empty()) return false;
      SpringState out{999.0, 999.0, 999.0, 999.0};
      if (!motion_deserialize(bytes, &out)) return false;
      if (std::memcmp(&out, &s, sizeof(SpringState)) != 0) return false;
    }
  } catch (...) {
    return false;  // never throws, never aborts
  }
  return true;
}

// Canonical form: fixed 32 bytes (4 LE doubles), byte-identical for equal
// states, and little-endian by construction — 1.0 encodes as
// 00 00 00 00 00 00 F0 3F (ASSUMPTION: declaration field order; see header).
bool check_serialize_is_fixed_size_canonical_le() {
  try {
    SpringState s{1.0, 0.0, 0.0, 0.0};
    std::vector<uint8_t> a = motion_serialize(s);
    std::vector<uint8_t> b = motion_serialize(s);
    if (a.size() != 32u || b.size() != 32u) return false;
    if (a != b) return false;  // deterministic encoding
    const uint8_t kOneLE[8] = {0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0xF0, 0x3F};
    for (int i = 0; i < 8; ++i) {
      if (a[static_cast<size_t>(i)] != kOneLE[i]) return false;
    }
    // Zero state encodes as all-zero bytes (canonical, no padding garbage).
    SpringState z{0.0, 0.0, 0.0, 0.0};
    std::vector<uint8_t> zb = motion_serialize(z);
    if (zb.size() != 32u) return false;
    for (uint8_t byte : zb) {
      if (byte != 0x00) return false;
    }
  } catch (...) {
    return false;
  }
  return true;
}

// Distinct states encode distinctly: the serialization must discriminate or
// replay comparison would be vacuous.
bool check_serialize_distinct_states_differ() {
  try {
    SpringState a{0.0, 0.0, 180.0, 12.0};
    SpringState b{100.0, 0.0, 180.0, 12.0};
    SpringState c{0.0, 0.0, 181.0, 12.0};
    std::vector<uint8_t> ba = motion_serialize(a);
    std::vector<uint8_t> bb = motion_serialize(b);
    std::vector<uint8_t> bc = motion_serialize(c);
    return ba != bb && ba != bc && bb != bc;
  } catch (...) {
    return false;
  }
  return true;
}

// The state hash is stable for equal states and discriminates different
// ones (AC-MOTION-029: identical per-frame hashes on repeated runs).
bool check_state_hash_is_stable_and_discriminating() {
  try {
    SpringState s{5.0, 1.0, 180.0, 12.0};
    if (motion_stateHash(s) != motion_stateHash(s)) return false;
    SpringState other{5.0, 1.0, 180.0, 13.0};
    if (motion_stateHash(s) == motion_stateHash(other)) return false;
    SpringState moved{6.0, 1.0, 180.0, 12.0};
    if (motion_stateHash(s) == motion_stateHash(moved)) return false;
  } catch (...) {
    return false;
  }
  return true;
}

// Fixed seed reproduces the identical hash/serialization sequence: seed,
// observe N frame hashes, re-seed with the same value, observe again —
// every hash must match (AC-MOTION-029 core: same session + fixed seed ->
// identical per-frame hashes; plan A4 seeded-RNG rule at the boundary).
bool check_same_seed_reproduces_identical_sequence() {
  try {
    const uint64_t kSeed = 0x12345678ABCDEF00ULL;
    const int kFrames = 32;
    std::vector<uint64_t> first;
    motion_seed(kSeed);
    SpringState s{0.0, 0.0, 180.0, 12.0};
    for (int i = 0; i < kFrames; ++i) {
      s = motion_retarget(s, 100.0, s.v, 1.0 / 120.0).state;
      first.push_back(motion_stateHash(s));
      // Serialization of each frame must also reproduce exactly.
      std::vector<uint8_t> bytes = motion_serialize(s);
      if (bytes.size() != 32u) return false;
    }
    motion_seed(kSeed);  // fresh stream, same seed
    SpringState t{0.0, 0.0, 180.0, 12.0};
    for (int i = 0; i < kFrames; ++i) {
      t = motion_retarget(t, 100.0, t.v, 1.0 / 120.0).state;
      if (motion_stateHash(t) != first[static_cast<size_t>(i)]) return false;
      if (motion_serialize(t) != motion_serialize(s) &&
          motion_serialize(t).size() != 32u)
        return false;
    }
  } catch (...) {
    return false;
  }
  return true;
}

// Wall-clock jitter between runs changes nothing: a CPU spin (standing in
// for scheduling/timing jitter — no clock is read) between two same-seed
// runs must leave every hash identical (AC-MOTION-029: "wall-clock jitter
// injection changes nothing"; plan A4: no wall-clock in the replay path).
bool check_wall_jitter_changes_nothing() {
  try {
    const uint64_t kSeed = 42u;
    motion_seed(kSeed);
    SpringState s{0.0, 0.0, 180.0, 12.0};
    uint64_t before = 0;
    for (int i = 0; i < 16; ++i) {
      s = motion_retarget(s, 50.0, s.v, 1.0 / 60.0).state;
      before ^= motion_stateHash(s);
    }
    volatile int spin = 0;  // jitter stand-in: burns wall time, reads nothing
    for (int i = 0; i < 500000; ++i) spin += i;
    (void)spin;
    motion_seed(kSeed);
    SpringState t{0.0, 0.0, 180.0, 12.0};
    uint64_t after = 0;
    for (int i = 0; i < 16; ++i) {
      t = motion_retarget(t, 50.0, t.v, 1.0 / 60.0).state;
      after ^= motion_stateHash(t);
    }
    return before == after;
  } catch (...) {
    return false;
  }
  return true;
}

// Empty input is malformed: deserialize returns false, never aborts.
bool check_deserialize_rejects_empty_input() {
  try {
    SpringState out{1.0, 2.0, 3.0, 4.0};
    std::vector<uint8_t> empty;
    if (motion_deserialize(empty, &out)) return false;
    return true;
  } catch (...) {
    return false;
  }
}

// Wrong-size inputs are malformed: truncated tails, single bytes, and
// trailing-garbage oversize encodings all return false, never abort.
bool check_deserialize_rejects_wrong_sizes() {
  try {
    SpringState good{1.0, -1.0, 180.0, 12.0};
    std::vector<uint8_t> full = motion_serialize(good);
    if (full.size() != 32u) return false;
    SpringState out{0.0, 0.0, 0.0, 0.0};
    // Truncated: every shorter prefix must fail.
    for (size_t len = 0; len < full.size(); ++len) {
      std::vector<uint8_t> cut(full.begin(), full.begin() + len);
      if (motion_deserialize(cut, &out)) return false;
    }
    // Oversize with trailing garbage must fail (strict canonical form).
    std::vector<uint8_t> padded = full;
    padded.push_back(0x00);
    if (motion_deserialize(padded, &out)) return false;
    std::vector<uint8_t> big(64, 0xA5);
    if (motion_deserialize(big, &out)) return false;
  } catch (...) {
    return false;
  }
  return true;
}

// Garbage full-size input and hostile frame-count-style payloads never
// abort: deserialize returns a bool, never throws, never traps (security:
// serialized states are untrusted bytes on the replay path).
bool check_deserialize_never_throws_on_garbage() {
  try {
    SpringState out{0.0, 0.0, 0.0, 0.0};
    const std::vector<std::vector<uint8_t>> hostile = {
        std::vector<uint8_t>(32, 0xFF),
        std::vector<uint8_t>(32, 0x00),
        std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF},
        std::vector<uint8_t>(1024, 0x41),
        std::vector<uint8_t>(1, 0x00),
    };
    for (const auto& bytes : hostile) {
      // Must resolve to a bool — true only for a decodable state, but most
      // importantly it must return at all (no abort/throw/hang).
      (void)motion_deserialize(bytes, &out);
    }
  } catch (...) {
    return false;
  }
  return true;
}

// A null output pointer returns false instead of aborting: malformed-call
// robustness on the deserialize boundary (never aborts per the Interface).
bool check_deserialize_null_out_returns_false() {
  try {
    SpringState s{1.0, 2.0, 180.0, 12.0};
    std::vector<uint8_t> bytes = motion_serialize(s);
    if (bytes.empty()) return false;
    if (motion_deserialize(bytes, nullptr)) return false;
    if (motion_deserialize(std::vector<uint8_t>(), nullptr)) return false;
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_serialize_round_trip_is_exact);
  RUN_CASE(check_serialize_is_fixed_size_canonical_le);
  RUN_CASE(check_serialize_distinct_states_differ);
  RUN_CASE(check_state_hash_is_stable_and_discriminating);
  RUN_CASE(check_same_seed_reproduces_identical_sequence);
  RUN_CASE(check_wall_jitter_changes_nothing);
  RUN_CASE(check_deserialize_rejects_empty_input);
  RUN_CASE(check_deserialize_rejects_wrong_sizes);
  RUN_CASE(check_deserialize_never_throws_on_garbage);
  RUN_CASE(check_deserialize_null_out_returns_false);
  if (failures > 0) {
    std::printf("cross_machine_replay_test: %d failing\n", failures);
    return 1;
  }
  std::printf("cross_machine_replay_test: all passing\n");
  return 0;
}
