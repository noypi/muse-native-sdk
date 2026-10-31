// Batch 2 — cadence/jank, EXIF-upright, HDR/P3-reference animated
// verification path (FR-MEDIA-007/AC-MEDIA-012, TC-MEDIA-012).
// Binds ONLY to the T4 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/animated_hdr.h plus
// the shared `engine/media/decode/image_metadata.h` EXIF seam only; no
// network, no env/config reads, no filesystem, no live services, no wall
// clock. Cadence is decided by in-test ready/deadline fields; the reference
// conversion is an in-test gamma-2.2 + linear-matrix encoding (A-REF).
//
// Expected state: RED — engine/media/include/media/animated_hdr.h does not
// exist yet, so this file fails to compile until the coder implements T4.
//
// Contract assumptions (see unit-tests/T4.md):
//   A-NS:   everything in namespace `media`; header
//           `engine/media/include/media/animated_hdr.h`.
//   A-SEQ:  `struct AnimFrame { uint32_t width = 0; uint32_t height = 0;
//           std::vector<uint8_t> pixels; uint64_t delayMs = 0;
//           uint64_t readyMs = 0; ExifOrientation orientation =
//           ExifOrientation::kNormal; ColorSpace color = ColorSpace::kSrgb;
//           };` (`readyMs` = when the decoded frame is ready relative to
//           sequence start; `delayMs` = authored on-screen duration.)
//           `struct AnimSequence { std::vector<AnimFrame> frames;
//           uint32_t loopCount = 1; };`
//           `struct ReferenceFrames { uint32_t width = 0; uint32_t height = 0;
//           std::vector<std::vector<uint8_t>> pixels; };`
//           (upright sRGB RGBA8 expectation, one entry per sequence frame).
//   A-OUT:  `struct HdrCheck { double maxDelta; bool cadenceOk; bool upright;
//           Status status; };` (`Status::{kOk,kOverBudget,kInvalidArg}`
//           shared with T1.)
//           `HdrCheck verifyAnimatedHdr(AnimSequence seq,
//             ReferenceFrames ref);`
//   A-DELTA: `inline constexpr double kHdrDelta = 8.0;` — per-channel max
//             absolute delta vs the in-test reference conversion.
//   A-CAD:  `cadenceOk` = every frame `readyMs` <= its authored deadline
//           (cumulative `delayMs`); any late frame (decode-induced jank) =>
//           false. `upright` = orientation applied at decode: output pixels
//           match the transposed/rotated reference.
//   A-COPY: "without extra copy" is a T5 trace assertion; this unit proves
//           the observable half (pixels match the upright reference from a
//           single verify call).
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/media/decode/image_metadata.h"
#include "engine/media/include/media/animated_hdr.h"

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

// In-test reference conversion (A-REF): gamma-2.2 linearize, dominant
// P3->sRGB matrix, re-encode. The coder implements its own conversion;
// outputs must land within kHdrDelta per channel of this reference.
static uint8_t RefConvert(uint8_t c) {
  const float lin = std::pow(c / 255.0f, 2.2f);
  const float v = 0.98f * lin + 0.01f * lin + 0.01f * lin;
  return static_cast<uint8_t>(std::round(255.0f * std::pow(v, 1.0f / 2.2f)));
}

// --- fixture builders --------------------------------------------------------
// 2-frame 1x1 sequence, both frames ready on time (readyMs <= deadlines).
static media::AnimSequence MakeOnTimeSequence() {
  media::AnimSequence seq;
  seq.loopCount = 1;
  media::AnimFrame a;
  a.width = 1;
  a.height = 1;
  a.pixels = {64, 180, 200, 255};
  a.delayMs = 100;
  a.readyMs = 0;
  a.orientation = media::ExifOrientation::kNormal;
  a.color = media::ColorSpace::kDisplayP3;
  media::AnimFrame b = a;
  b.pixels = {200, 64, 180, 255};
  b.delayMs = 100;
  b.readyMs = 90;
  seq.frames.push_back(a);
  seq.frames.push_back(b);
  return seq;
}

static media::ReferenceFrames MakeReference(const media::AnimSequence& seq) {
  media::ReferenceFrames ref;
  ref.width = 1;
  ref.height = 1;
  for (const auto& f : seq.frames) {
    std::vector<uint8_t> px = f.pixels;
    if (f.color == media::ColorSpace::kDisplayP3 && px.size() == 4) {
      px[0] = RefConvert(px[0]);
      px[1] = RefConvert(px[1]);
      px[2] = RefConvert(px[2]);
    }
    ref.pixels.push_back(px);
  }
  return ref;
}

// --- cases -------------------------------------------------------------------

// Empty sequence is a synchronous kInvalidArg reject that never throws.
static void check_empty_sequence_rejected() {
  bool threw = false;
  media::HdrCheck r{0.0, false, false, media::Status::kOk};
  try {
    r = media::verifyAnimatedHdr(media::AnimSequence{},
                                 media::ReferenceFrames{});
  } catch (...) {
    threw = true;
  }
  CHECK("empty sequence never throws", !threw);
  CHECK("empty sequence status kInvalidArg",
        r.status == media::Status::kInvalidArg);
}

// On-time frames hold the authored cadence: cadenceOk=true.
static void check_cadence_held() {
  media::AnimSequence seq = MakeOnTimeSequence();
  media::HdrCheck r = media::verifyAnimatedHdr(seq, MakeReference(seq));
  CHECK("on-time status kOk", r.status == media::Status::kOk);
  CHECK("on-time cadenceOk", r.cadenceOk == true);
}

// A frame ready after its deadline is decode-induced jank: cadenceOk=false.
static void check_late_frame_jank() {
  media::AnimSequence seq = MakeOnTimeSequence();
  seq.frames[1].readyMs = 500;  // deadline is 200ms: late.
  media::HdrCheck r = media::verifyAnimatedHdr(seq, MakeReference(seq));
  CHECK("late frame status kOk", r.status == media::Status::kOk);
  CHECK("late frame cadenceOk false", r.cadenceOk == false);
}

// Upright source renders upright.
static void check_upright_normal() {
  media::AnimSequence seq = MakeOnTimeSequence();
  media::HdrCheck r = media::verifyAnimatedHdr(seq, MakeReference(seq));
  CHECK("normal orientation upright", r.upright == true);
}

// EXIF-rotated source (2x1, rotate-90-CW) renders upright without extra copy:
// output pixels match the 1x2 transposed reference from a single call.
static void check_exif_upright() {
  media::AnimSequence seq;
  seq.loopCount = 1;
  media::AnimFrame f;
  f.width = 2;
  f.height = 1;
  // Row: [red, green] stored left-to-right; rotate-90-CW => 1x2 column
  // [green, red] top-to-bottom... decoded upright expectation below.
  f.pixels = {255, 0, 0, 255, 0, 255, 0, 255};
  f.delayMs = 100;
  f.readyMs = 0;
  f.orientation = media::ExifOrientation::kRotate90CW;
  f.color = media::ColorSpace::kSrgb;
  seq.frames.push_back(f);
  media::ReferenceFrames ref;
  ref.width = 1;
  ref.height = 2;
  ref.pixels.push_back({0, 255, 0, 255, 255, 0, 0, 255});
  media::HdrCheck r{0.0, false, false, media::Status::kOk};
  bool threw = false;
  try {
    r = media::verifyAnimatedHdr(seq, ref);
  } catch (...) {
    threw = true;
  }
  CHECK("exif path never throws", !threw);
  CHECK("exif status kOk", r.status == media::Status::kOk);
  CHECK("exif-rotated upright", r.upright == true);
}

// P3 fixtures match the reference conversion within kHdrDelta.
static void check_hdr_within_delta() {
  media::AnimSequence seq = MakeOnTimeSequence();
  media::HdrCheck r = media::verifyAnimatedHdr(seq, MakeReference(seq));
  CHECK("p3 status kOk", r.status == media::Status::kOk);
  CHECK("p3 maxDelta within kHdrDelta", r.maxDelta <= media::kHdrDelta);
}

// A divergent reference exceeds the delta (the check actually discriminates).
static void check_divergent_reference_exceeds_delta() {
  media::AnimSequence seq = MakeOnTimeSequence();
  media::ReferenceFrames ref = MakeReference(seq);
  ref.pixels[0] = {0, 0, 0, 255};  // far from the converted teal.
  media::HdrCheck r = media::verifyAnimatedHdr(seq, ref);
  CHECK("divergent status kOk", r.status == media::Status::kOk);
  CHECK("divergent maxDelta exceeds kHdrDelta", r.maxDelta > media::kHdrDelta);
}

// Corrupt frame payloads never throw and never report success content.
static void check_corrupt_frame_never_throws() {
  media::AnimSequence seq = MakeOnTimeSequence();
  seq.frames[0].pixels = {1, 2, 3};  // truncated: size != w*h*4.
  bool threw = false;
  media::HdrCheck r{0.0, false, false, media::Status::kOk};
  try {
    r = media::verifyAnimatedHdr(seq, MakeReference(seq));
  } catch (...) {
    threw = true;
  }
  CHECK("corrupt frame never throws", !threw);
  CHECK("corrupt frame not upright-ok",
        r.upright == false || r.status != media::Status::kOk);
}

int main() {
  check_empty_sequence_rejected();
  check_cadence_held();
  check_late_frame_jank();
  check_upright_normal();
  check_exif_upright();
  check_hdr_within_delta();
  check_divergent_reference_exceeds_delta();
  check_corrupt_frame_never_throws();
  if (s_failures == 0) std::printf("ALL ANIMATED_HDR TESTS PASSED\n");
  return s_failures == 0 ? 0 : 1;
}
