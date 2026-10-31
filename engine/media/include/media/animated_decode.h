#pragma once

// T3 — animated decode with frame timing, loop counts,
// off-screen/reduce-motion pause (FR-MEDIA-007/AC-MEDIA-010, TC-MEDIA-010).
//
// Hermetic: C++17, pure in-memory timing over inline delay tables; the clock
// is the explicit `nowMs` argument (never the wall clock); reduce-motion
// input arrives via `motionReduced()` (declared here, defined test-locally;
// `engine/motion` is consumed as a seam, never linked). Never throws.

#include <cstdint>
#include <vector>

namespace media {

enum class AssetError { kOk, kNotPrepared, kInvalidArg };

// Single animation frame (RGBA8 row-major; 1x1 stub pixels in tests).
struct Frame {
  uint32_t index = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;
  uint64_t delayMs = 0;
};

// Player state. Default-constructs UNPREPARED; prepared via `animLoad`.
// `loopCount` = total plays (0 = infinite).
struct AnimPlayer {
  bool prepared = false;
  std::vector<uint64_t> delays;
  uint32_t loopCount = 0;
  uint32_t heldIndex = 0;
};

struct AnimStatus {
  Frame f;
  bool paused = false;
  AssetError err = AssetError::kOk;
};

// Test/setup seam (not in the plan `Interface:` line; see unit-tests/T3.md
// ASSUMPTION A-PLAYER): stores the delay table + loop count.
//  - empty `delaysMs` -> kInvalidArg, player stays unprepared.
//  - otherwise -> kOk, player prepared, timeline origin = load (t=0).
// Never throws.
AssetError animLoad(AnimPlayer& p, const std::vector<uint64_t>& delaysMs,
                    uint32_t loopCount) noexcept;

// Advances to the live frame for `nowMs` (pure function of `nowMs` since
// load; t=0 shows frame 0).
//  - unprepared -> err kNotPrepared (frame/paused unspecified).
//  - per-frame delays honored; finite loops repeat `loopCount` times then
//    clamp on the last frame with kOk; 0 loops = infinite.
//  - paused = (!onScreen || motionReduced()); a paused advance holds the
//    last shown frame (frame 0 if none shown yet) without shifting the
//    timeline, so resuming at the same `nowMs` shows the live frame.
//  - hostile clocks (e.g. UINT64_MAX) clamp safely.
// Never throws.
AnimStatus animAdvance(AnimPlayer& p, uint64_t nowMs, bool onScreen) noexcept;

// Reduce-motion probe (MOTION-A11Y seam). DECLARED here, DEFINED by the
// caller/test (`anim_test::g_testReduced` fake in tests); this translation
// unit must call it, never redefine it.
bool motionReduced();

}  // namespace media
