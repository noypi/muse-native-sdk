// Batch 2 contract tests — T4 (FR-MOTION-009 / AC-MOTION-027, TC-MOTION-027).
// Hermetic: C++17 + motion headers only; fixed dt nanos per frame, no
// wall-clock reads, no env/config, no filesystem, no network, no randomness.
// Follows the engine/motion convention (plain main(), RUN_CASE macro, printf
// PASS/FAIL, nonzero exit on failure; cf. tests/retarget_test.cpp).
//
// Binds to the plan item's `Interface:` line (reproduced verbatim in
// unit-tests/T4.md):
//   struct Recording { std::vector<uint8_t> bytes; uint64_t frameCount; };
//   Recording motion_recordStart(); void motion_recordFrame(const SpringState&
//     s, int64_t dtNanos); ReplayStatus motion_replay(const Recording& r);
//   enum class ReplayStatus { kIdentical, kDiverged, kCorrupt };
//   corrupt input returns kCorrupt, never crashes.
//
// T1.3 ESCALATION (finish accessor — see unit-tests/T4.md Conflicts and
// Gaps): the named trio has no way to obtain the finished Recording for
// replay (motion_recordStart returns the empty initial state and
// motion_recordFrame takes no Recording out-param). The round-trip and
// corruption tests below assume the conventional completion
//   Recording motion_recordFinish();
// which the planner/coder must confirm or replace; ONLY the call sites
// (not the expectations) will be reconciled to the settled name. Every
// other test uses the verbatim named interface only.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "motion/recorder.h"
#include "motion/spring.h"

namespace {

const int64_t kDtNanos = 8333333;  // ~120 Hz frame in nanos (fixed, no clock)

// Drives a short retarget session (T1 API, already implemented) and records
// each step. Purely deterministic: fixed inputs, fixed dt.
void recordRetargetSession(int extraFrames) {
  SpringState s{0.0, 0.0, 180.0, 12.0};
  for (int i = 0; i < 15; ++i) {
    s = motion_retarget(s, 100.0, s.v, 1.0 / 120.0).state;
  }
  motion_recordFrame(s, kDtNanos);
  RetargetResult r = motion_retarget(s, 200.0, s.v, 1.0 / 120.0);
  s = r.state;
  motion_recordFrame(s, kDtNanos);
  for (int i = 0; i < extraFrames; ++i) {
    s = motion_retarget(s, 200.0, s.v, 1.0 / 120.0).state;
    motion_recordFrame(s, kDtNanos);
  }
}

// A fresh recording is empty: zero frames, no bytes.
bool check_record_start_is_empty() {
  Recording r = motion_recordStart();
  return r.frameCount == 0 && r.bytes.empty();
}

// Recording frames never throws, including extreme-but-finite states.
bool check_record_frame_never_throws() {
  try {
    motion_recordStart();
    SpringState s{1.5, -2.5, 180.0, 12.0};
    motion_recordFrame(s, kDtNanos);
    SpringState extreme{1e15, -1e15, 1e6, 1e6};
    motion_recordFrame(extreme, kDtNanos);
    SpringState zero{0.0, 0.0, 180.0, 12.0};
    motion_recordFrame(zero, kDtNanos);
  } catch (...) {
    return false;
  }
  return true;
}

// Record-then-replay of a retarget session yields a byte-identical state
// sequence (AC-MOTION-027 core acceptance). Requires the finish accessor
// under T1.3 escalation (see file header).
bool check_record_then_replay_is_byte_identical() {
  try {
    motion_recordStart();
    recordRetargetSession(30);
    Recording r = motion_recordFinish();
    if (r.frameCount != 32) return false;
    if (r.bytes.empty()) return false;
    return motion_replay(r) == ReplayStatus::kIdentical;
  } catch (...) {
    return false;
  }
}

// A single-bit flip anywhere in a valid recording never passes silently:
// replay must report kCorrupt or kDiverged, never kIdentical (AC-MOTION-027).
// Requires the finish accessor under T1.3 escalation (see file header).
bool check_single_bit_corruption_never_passes_silently() {
  try {
    motion_recordStart();
    recordRetargetSession(30);
    Recording r = motion_recordFinish();
    if (r.bytes.empty()) return false;
    for (size_t i = 0; i < r.bytes.size(); ++i) {
      Recording damaged = r;
      damaged.bytes[i] ^= 0x01;  // flip exactly one bit
      ReplayStatus st = motion_replay(damaged);
      if (st == ReplayStatus::kIdentical) return false;
    }
    return true;
  } catch (...) {
    return false;  // never crashes, even while probing corrupt inputs
  }
}

// Truncation is corruption: dropping the tail of the byte stream must yield
// kCorrupt, never kIdentical and never a crash.
bool check_truncated_recording_returns_corrupt() {
  try {
    motion_recordStart();
    recordRetargetSession(10);
    Recording r = motion_recordFinish();
    if (r.bytes.size() < 2) return false;
    Recording cut = r;
    cut.bytes.resize(cut.bytes.size() / 2);  // keep frameCount, halve bytes
    if (motion_replay(cut) == ReplayStatus::kIdentical) return false;
    Recording emptied = r;
    emptied.bytes.clear();  // nonzero frameCount, zero bytes
    return motion_replay(emptied) == ReplayStatus::kCorrupt;
  } catch (...) {
    return false;
  }
}

// A nonzero frameCount with empty bytes is corrupt by construction (needs no
// session at all; pure struct literal through the public interface).
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
// counts, and header-length mismatches all resolve to kCorrupt/kDiverged.
// (Security: malformed recordings are untrusted input; the replay path
// must not abort, throw, or hang on them.)
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

// ASSUMPTION (vacuous replay): an empty recording replays kIdentical —
// there are zero frames to diverge on. Flagged because the plan does not
// pin the empty-recording status; the load-bearing property is that it
// resolves deterministically without crashing.
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
  RUN_CASE(check_record_frame_never_throws);
  RUN_CASE(check_record_then_replay_is_byte_identical);
  RUN_CASE(check_single_bit_corruption_never_passes_silently);
  RUN_CASE(check_truncated_recording_returns_corrupt);
  RUN_CASE(check_empty_bytes_with_nonzero_count_is_corrupt);
  RUN_CASE(check_replay_never_throws_on_malformed_input);
  RUN_CASE(check_empty_recording_replays_identical);
  if (failures > 0) {
    std::printf("replay_determinism_test: %d failing\n", failures);
    return 1;
  }
  std::printf("replay_determinism_test: all passing\n");
  return 0;
}
