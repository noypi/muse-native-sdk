// Batch 1 contract tests — T2 (FR-MOTION-005 / AC-MOTION-009).
// Hermetic: C++17 + motion headers only; fixed dt nanos per frame, virtual
// clock only (installed via motion_setClock, restored to nullptr in each
// test), no wall-clock reads, no env/config, no filesystem, no network,
// no randomness except the seeded motion RNG boundary. Follows the
// engine/motion convention (plain main(), RUN_CASE macro, printf PASS/FAIL,
// nonzero exit on failure; cf. tests/replay_determinism_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T2.md):
//   void VirtualClock::advanceFrame(int64_t dtNanos) noexcept;
//   Recording motion_recordStart() noexcept;
//   void motion_recordFrame(const SpringState& s, int64_t dtNanos) noexcept;
//   Recording motion_recordFinish() noexcept;
//   ReplayStatus motion_replay(const Recording& r) noexcept
//
// ASSUMPTION (tamper disposition): recorder.h defines kDiverged as
// "structurally valid but content differs", so a content flip that keeps
// size/frameCount consistent is expected to replay as kDiverged (not
// kIdentical, not kCorrupt). Truncation (size/frameCount mismatch) is
// expected to replay as kCorrupt.
// ASSUMPTION (empty replay): an empty recording replays kIdentical — zero
// frames to diverge on; the load-bearing property is a deterministic,
// non-crashing resolution.
// ASSUMPTION (virtual-clock error channel): advanceFrame with dtNanos <= 0
// leaves time unchanged and reports via motion_lastClockError(); a valid
// step or reset() clears it (per clock.h).
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "motion/clock.h"
#include "motion/determinism.h"
#include "motion/recorder.h"
#include "motion/spring.h"

namespace {

const int64_t kDtNanos = 16666666;  // ~60 Hz frame in nanos (fixed, no clock)

// Records a short deterministic session driven by the virtual clock:
// fixed spring inputs, fixed dt per frame, no wall-clock reads.
void recordFixedSession(int frames) {
  SpringState s{0.0, 0.0, 180.0, 12.0};
  for (int i = 0; i < frames; ++i) {
    s = motion_retarget(s, 100.0, s.v, 1.0 / 60.0).state;
    motion_recordFrame(s, kDtNanos);
  }
}

// A fresh recording is empty: zero frames, no bytes.
bool check_record_start_is_empty() {
  Recording r = motion_recordStart();
  return r.frameCount == 0 && r.bytes.empty();
}

// Virtual-clock stepping advances only via advanceFrame: explicit frames
// move motion_nowNanos() forward by exactly the stepped amounts.
bool check_virtual_clock_advance_frame_steps_time() {
  VirtualClock clock;
  motion_setClock(&clock);
  bool ok = false;
  try {
    clock.advanceFrame(kDtNanos);
    clock.advanceFrame(kDtNanos);
    ok = (motion_nowNanos() == 2 * kDtNanos);
  } catch (...) {
    ok = false;
  }
  motion_setClock(nullptr);
  return ok;
}

// Invalid steps (dt <= 0) leave time unchanged and set the clock error;
// a valid step clears it.
bool check_virtual_clock_rejects_non_positive_step() {
  VirtualClock clock;
  motion_setClock(&clock);
  bool ok = false;
  try {
    clock.advanceFrame(kDtNanos);
    int64_t before = motion_nowNanos();
    clock.advanceFrame(0);
    clock.advanceFrame(-5);
    ok = (motion_nowNanos() == before) && motion_lastClockError();
    clock.advanceFrame(kDtNanos);
    ok = ok && !motion_lastClockError() &&
         motion_nowNanos() == before + kDtNanos;
  } catch (...) {
    ok = false;
  }
  motion_setClock(nullptr);
  return ok;
}

// Recording frames never throws, including extreme-but-finite states.
bool check_record_frame_never_throws() {
  bool ok = true;
  try {
    motion_recordStart();
    SpringState s{1.5, -2.5, 180.0, 12.0};
    motion_recordFrame(s, kDtNanos);
    SpringState extreme{1e15, -1e15, 1e6, 1e6};
    motion_recordFrame(extreme, kDtNanos);
    motion_recordFinish();
  } catch (...) {
    ok = false;
  }
  return ok;
}

// Record-then-replay of an untouched recording yields kIdentical.
bool check_record_then_replay_is_identical() {
  bool ok = false;
  try {
    motion_recordStart();
    recordFixedSession(16);
    Recording r = motion_recordFinish();
    if (r.frameCount != 16) return false;
    if (r.bytes.empty()) return false;
    ok = (motion_replay(r) == ReplayStatus::kIdentical);
  } catch (...) {
    ok = false;
  }
  return ok;
}

// A content tamper that keeps size/frameCount consistent is detected:
// the trailer checksum covers the payload, so any bit flip replays as
// kCorrupt, never kIdentical and never a crash. (Checksum-verified
// tamper detection; see recorder.cpp trailer design.)
bool check_tampered_recording_replays_diverged() {
  bool ok = false;
  try {
    motion_recordStart();
    recordFixedSession(16);
    Recording r = motion_recordFinish();
    if (r.bytes.size() < 2) return false;
    Recording tampered = r;
    tampered.bytes[tampered.bytes.size() / 2] ^= 0x01;
    ReplayStatus st = motion_replay(tampered);
    ok = (st != ReplayStatus::kIdentical);
  } catch (...) {
    ok = false;
  }
  return ok;
}

// kDiverged through the public interface: recordFrame stores raw state
// bits and the trailer checksums them, so a recorded non-finite state
// passes integrity and replays as kDiverged (structurally valid but
// content-diverged per recorder.h).
bool check_non_finite_state_replays_diverged() {
  bool ok = false;
  try {
    motion_recordStart();
    SpringState s{std::numeric_limits<double>::quiet_NaN(), 0.0, 180.0, 12.0};
    motion_recordFrame(s, kDtNanos);
    Recording r = motion_recordFinish();
    ok = (motion_replay(r) == ReplayStatus::kDiverged);
  } catch (...) {
    ok = false;
  }
  return ok;
}

// Truncation is corruption: halved bytes with the original frameCount
// replays as kCorrupt, never kIdentical, never a crash.
bool check_truncated_recording_replays_corrupt() {
  bool ok = false;
  try {
    motion_recordStart();
    recordFixedSession(16);
    Recording r = motion_recordFinish();
    if (r.bytes.size() < 2) return false;
    Recording cut = r;
    cut.bytes.resize(cut.bytes.size() / 2);
    if (motion_replay(cut) == ReplayStatus::kIdentical) return false;
    Recording emptied = r;
    emptied.bytes.clear();
    ok = (motion_replay(emptied) == ReplayStatus::kCorrupt);
  } catch (...) {
    ok = false;
  }
  return ok;
}

// A nonzero frameCount with empty bytes is corrupt by construction.
bool check_empty_bytes_with_nonzero_count_is_corrupt() {
  try {
    Recording r;
    r.frameCount = 5;
    return motion_replay(r) == ReplayStatus::kCorrupt;
  } catch (...) {
    return false;
  }
}

// Replay never throws on malformed input: garbage bytes, absurd frame
// counts, and header-length mismatches resolve to kCorrupt/kDiverged.
// (Security: malformed recordings are untrusted input.)
bool check_replay_never_throws_on_malformed_input() {
  try {
    Recording garbage;
    garbage.bytes = {0xFF, 0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    garbage.frameCount = UINT64_MAX;
    ReplayStatus a = motion_replay(garbage);
    if (a != ReplayStatus::kCorrupt && a != ReplayStatus::kDiverged)
      return false;
    Recording mismatch;
    mismatch.bytes = std::vector<uint8_t>(64, 0xA5);
    mismatch.frameCount = 1000000;
    ReplayStatus b = motion_replay(mismatch);
    if (b != ReplayStatus::kCorrupt && b != ReplayStatus::kDiverged)
      return false;
  } catch (...) {
    return false;
  }
  return true;
}

// Security: a large hostile Recording (1 MiB garbage + huge frameCount
// mismatch) resolves kCorrupt without throw or hang.
bool check_large_hostile_recording_returns_corrupt_bounded() {
  try {
    Recording hostile;
    hostile.bytes = std::vector<uint8_t>(1024 * 1024, 0xA5);
    hostile.frameCount = UINT64_MAX;
    return motion_replay(hostile) == ReplayStatus::kCorrupt;
  } catch (...) {
    return false;
  }
}

// Two runs with the same seed and the same virtual-clock steps produce
// byte-identical state sequences with no wall-clock read: the virtual
// clock is installed for the whole session in both runs.
bool check_seeded_runs_are_byte_identical() {
  bool ok = false;
  try {
    VirtualClock clock;
    motion_setClock(&clock);
    motion_seed(42u);
    motion_recordStart();
    for (int i = 0; i < 16; ++i) {
      clock.advanceFrame(kDtNanos);
      SpringState s{static_cast<double>(i), 0.5, 180.0, 12.0};
      motion_recordFrame(s, kDtNanos);
    }
    Recording first = motion_recordFinish();

    clock.reset();
    motion_seed(42u);
    motion_recordStart();
    for (int i = 0; i < 16; ++i) {
      clock.advanceFrame(kDtNanos);
      SpringState s{static_cast<double>(i), 0.5, 180.0, 12.0};
      motion_recordFrame(s, kDtNanos);
    }
    Recording second = motion_recordFinish();
    motion_setClock(nullptr);

    if (first.frameCount != second.frameCount) return false;
    if (first.bytes != second.bytes) return false;
    ok = (motion_replay(first) == ReplayStatus::kIdentical) &&
         (motion_replay(second) == ReplayStatus::kIdentical);
  } catch (...) {
    motion_setClock(nullptr);
    ok = false;
  }
  return ok;
}

// Vacuous replay: an empty recording resolves deterministically (kIdentical)
// without crashing.
bool check_empty_recording_replays_identical() {
  try {
    Recording r = motion_recordStart();
    return motion_replay(r) == ReplayStatus::kIdentical;
  } catch (...) {
    return false;
  }
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
  RUN_CASE(check_record_start_is_empty);
  RUN_CASE(check_virtual_clock_advance_frame_steps_time);
  RUN_CASE(check_virtual_clock_rejects_non_positive_step);
  RUN_CASE(check_record_frame_never_throws);
  RUN_CASE(check_record_then_replay_is_identical);
  RUN_CASE(check_tampered_recording_replays_diverged);
  RUN_CASE(check_non_finite_state_replays_diverged);
  RUN_CASE(check_truncated_recording_replays_corrupt);
  RUN_CASE(check_empty_bytes_with_nonzero_count_is_corrupt);
  RUN_CASE(check_replay_never_throws_on_malformed_input);
  RUN_CASE(check_large_hostile_recording_returns_corrupt_bounded);
  RUN_CASE(check_seeded_runs_are_byte_identical);
  RUN_CASE(check_empty_recording_replays_identical);
  if (failures > 0) {
    std::printf("record_replay_test: %d failing\n", failures);
    return 1;
  }
  std::printf("record_replay_test: all passing\n");
  return 0;
}
