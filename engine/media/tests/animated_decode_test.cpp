// Batch 2 — animated decode with frame timing, loop counts,
// off-screen/reduce-motion pause (FR-MEDIA-007/AC-MEDIA-010, TC-MEDIA-010).
// Binds ONLY to the T3 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/animated_decode.h
// only; no network, no env/config reads, no filesystem, no live services.
// The clock is the explicit `nowMs` argument (never the wall clock); the
// reduce-motion input is a test-local fake (see A-MOTION); all fixtures are
// inline delay tables.
//
// Expected state: RED — engine/media/include/media/animated_decode.h does
// not exist yet, so this file fails to compile until the coder implements T3.
//
// Contract assumptions (see unit-tests/T3.md):
//   A-NS:     everything in namespace `media`; header
//             `engine/media/include/media/animated_decode.h`.
//   A-ERR:    `enum class AssetError { kOk, kNotPrepared, kInvalidArg };`.
//   A-FRAME:  `struct Frame { uint32_t index = 0; uint32_t width = 0;
//             uint32_t height = 0; std::vector<uint8_t> pixels;
//             uint64_t delayMs = 0; };` (RGBA8 row-major, 1x1 fixtures here).
//   A-PLAYER: `struct AnimPlayer` default-constructs UNPREPARED; prepared via
//             `AssetError animLoad(AnimPlayer& p,
//               const std::vector<uint64_t>& delaysMs, uint32_t loopCount)`
//             (`loopCount` = total plays; 0 = infinite). Empty `delaysMs`
//             rejects with `kInvalidArg` and leaves the player unprepared.
//   A-STATUS: `struct AnimStatus { Frame f; bool paused; AssetError err; };`
//             `AnimStatus animAdvance(AnimPlayer& p, uint64_t nowMs,
//               bool onScreen)`.
//   A-MOTION: the header declares (never defines) `bool motionReduced();`
//             the test file defines it test-locally over `g_testReduced`.
//             Production `animated_decode.cc` must call it, not redefine it.
//   A-TIME:   frame selection is a pure function of `nowMs` since load
//             (t=0 shows frame 0); pauses hold the current frame without
//             advancing the timeline.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/media/include/media/animated_decode.h"

// --- test-local motion fake (A-MOTION) ---------------------------------------
namespace media {
namespace anim_test {
static bool g_testReduced = false;
}  // namespace anim_test
bool motionReduced() { return anim_test::g_testReduced; }
}  // namespace media

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// --- fixture builders --------------------------------------------------------
static media::AnimPlayer MakePlayer(const std::vector<uint64_t>& delays,
                                    uint32_t loops) {
  media::AnimPlayer p;
  media::AssetError e = media::animLoad(p, delays, loops);
  (void)e;
  return p;
}

// --- cases -------------------------------------------------------------------

// Advance before any load is a synchronous kNotPrepared reject, never throws.
static void check_advance_before_load_rejected() {
  media::AnimPlayer p;  // default-constructed: unprepared (A-PLAYER).
  bool threw = false;
  media::AnimStatus s{{{0}, 0, 0, {}, 0}, true, media::AssetError::kOk};
  try {
    s = media::animAdvance(p, 0u, true);
  } catch (...) {
    threw = true;
  }
  CHECK("advance before load never throws", !threw);
  CHECK("advance before load err kNotPrepared",
        s.err == media::AssetError::kNotPrepared);
}

// Loading an empty delay table rejects kInvalidArg and stays unprepared.
static void check_empty_load_rejected() {
  media::AnimPlayer p;
  bool threw = false;
  media::AssetError e = media::AssetError::kOk;
  try {
    e = media::animLoad(p, {}, 1u);
  } catch (...) {
    threw = true;
  }
  CHECK("empty load never throws", !threw);
  CHECK("empty load kInvalidArg", e == media::AssetError::kInvalidArg);
  media::AnimStatus s = media::animAdvance(p, 0u, true);
  CHECK("rejected player still kNotPrepared",
        s.err == media::AssetError::kNotPrepared);
}

// t=0 on a prepared player shows frame 0, unpaused, kOk.
static void check_first_frame_at_zero() {
  media::AnimPlayer p = MakePlayer({100u, 100u}, 1u);
  media::anim_test::g_testReduced = false;
  media::AnimStatus s = media::animAdvance(p, 0u, true);
  CHECK("first frame kOk", s.err == media::AssetError::kOk);
  CHECK("first frame not paused", s.paused == false);
  CHECK("first frame index 0", s.f.index == 0u);
}

// Per-frame delays are honored: mid-delay holds, post-delay advances.
static void check_frame_delays_honored() {
  media::AnimPlayer p = MakePlayer({100u, 200u}, 1u);
  media::anim_test::g_testReduced = false;
  CHECK("t=50 holds frame 0",
        media::animAdvance(p, 50u, true).f.index == 0u);
  CHECK("t=100 advances to frame 1",
        media::animAdvance(p, 100u, true).f.index == 1u);
  CHECK("t=250 holds frame 1",
        media::animAdvance(p, 250u, true).f.index == 1u);
  CHECK("t=300 clamps to last frame of single loop",
        media::animAdvance(p, 300u, true).f.index == 1u);
}

// Loop counts repeat the sequence; exhausted loops clamp on the last frame.
static void check_loop_counts_honored() {
  media::AnimPlayer p = MakePlayer({100u, 100u}, 2u);
  media::anim_test::g_testReduced = false;
  CHECK("loop2 t=0 frame 0",
        media::animAdvance(p, 0u, true).f.index == 0u);
  CHECK("loop2 t=100 frame 1",
        media::animAdvance(p, 100u, true).f.index == 1u);
  CHECK("loop2 t=200 wraps to frame 0",
        media::animAdvance(p, 200u, true).f.index == 0u);
  CHECK("loop2 t=300 frame 1",
        media::animAdvance(p, 300u, true).f.index == 1u);
  media::AnimStatus end = media::animAdvance(p, 100000u, true);
  CHECK("exhausted loops stay kOk", end.err == media::AssetError::kOk);
  CHECK("exhausted loops clamp to last frame", end.f.index == 1u);
}

// Off-screen advance pauses and holds the frame without consuming time.
static void check_offscreen_pauses() {
  media::AnimPlayer p = MakePlayer({100u, 100u}, 0u);
  media::anim_test::g_testReduced = false;
  media::AnimStatus s = media::animAdvance(p, 150u, false);
  CHECK("off-screen paused", s.paused == true);
  CHECK("off-screen still kOk", s.err == media::AssetError::kOk);
  CHECK("off-screen holds frame 0 at t=150 (no time consumed)",
        s.f.index == 0u);
}

// Reduced motion pauses even when on-screen.
static void check_reduce_motion_pauses() {
  media::AnimPlayer p = MakePlayer({100u, 100u}, 0u);
  media::anim_test::g_testReduced = true;
  media::AnimStatus s = media::animAdvance(p, 150u, true);
  CHECK("reduced motion paused", s.paused == true);
  CHECK("reduced motion still kOk", s.err == media::AssetError::kOk);
  media::anim_test::g_testReduced = false;
}

// Clearing both pause inputs resumes from the held position.
static void check_resume_after_pause() {
  media::AnimPlayer p = MakePlayer({100u, 100u}, 0u);
  media::anim_test::g_testReduced = false;
  (void)media::animAdvance(p, 150u, false);  // paused hold.
  media::AnimStatus s = media::animAdvance(p, 150u, true);
  CHECK("resume not paused", s.paused == false);
  CHECK("resume kOk", s.err == media::AssetError::kOk);
  CHECK("resume shows live frame 1", s.f.index == 1u);
}

// Hostile clock values never throw and keep a valid shape.
static void check_hostile_clock_never_throws() {
  media::AnimPlayer p = MakePlayer({100u, 100u}, 0u);
  media::anim_test::g_testReduced = false;
  bool threw = false;
  media::AnimStatus s{{{0}, 0, 0, {}, 0}, false, media::AssetError::kOk};
  try {
    s = media::animAdvance(p, UINT64_MAX, true);
  } catch (...) {
    threw = true;
  }
  CHECK("max clock never throws", !threw);
  CHECK("max clock valid err",
        s.err == media::AssetError::kOk ||
            s.err == media::AssetError::kNotPrepared);
}

int main() {
  check_advance_before_load_rejected();
  check_empty_load_rejected();
  check_first_frame_at_zero();
  check_frame_delays_honored();
  check_loop_counts_honored();
  check_offscreen_pauses();
  check_reduce_motion_pauses();
  check_resume_after_pause();
  check_hostile_clock_never_throws();
  if (s_failures == 0) std::printf("ALL ANIMATED_DECODE TESTS PASSED\n");
  return s_failures == 0 ? 0 : 1;
}
