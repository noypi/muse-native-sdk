#ifndef MOTION_RECORDER_H_
#define MOTION_RECORDER_H_

// Deterministic session recorder / replayer (FR-MOTION-009 / AC-MOTION-027).
//
// Captures per-frame inputs under the virtual clock (T3): start opens an
// empty session, frame appends one step, finish returns the snapshot and
// closes the session. Replay re-evaluates the recorded frames and compares
// state hashes frame by frame. Corrupt input yields kCorrupt, never a crash
// or throw. Nothing here throws.

#include <cstdint>
#include <vector>

#include "motion/spring.h"

struct Recording {
  std::vector<uint8_t> bytes;
  uint64_t frameCount = 0;
};

enum class ReplayStatus {
  kIdentical,
  kDiverged,
  kCorrupt,
};

// Opens an empty recording session and returns its empty snapshot.
// Never throws.
Recording motion_recordStart() noexcept;

// Appends one frame (state + dt) to the open session. Never throws,
// including extreme-but-finite states.
void motion_recordFrame(const SpringState& s, int64_t dtNanos) noexcept;

// Returns the finished session snapshot and closes the session.
// Never throws.
Recording motion_recordFinish() noexcept;

// Replays a recording, comparing state hashes frame by frame.
// kIdentical: byte-identical state sequence; kDiverged: structurally valid
// but content differs; kCorrupt: truncated/mismatched/garbage input.
// Never throws, never crashes, never hangs on malformed input.
ReplayStatus motion_replay(const Recording& r) noexcept;

#endif  // MOTION_RECORDER_H_
