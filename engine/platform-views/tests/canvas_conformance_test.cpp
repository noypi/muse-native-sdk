// T5 Canvas conformance suite — FR-PVIEWS-014 (Picture replay at scales).
// Binds ONLY to the plan T5 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/picture.h (to-be-created) only; no device lab,
// no android.graphics.Canvas, no network, no env/config reads, no
// filesystem, no live services. Conformance compares against a CPU
// reference raster host-side within TEST-GOLDEN tolerance (plan A4).
// Errors are return values, never thrown.
//
// RED state (Batch 3, pre-coder): pviews/picture.h and src/picture.cpp do
// not exist yet, so this file does NOT compile. That compile failure is
// the expected red for every case below.
//
// ASSUMED picture.h shape (see unit-tests/T5.md; coder must satisfy it;
// everything beyond the `Interface:` line is marked ASSUMPTION there):
//   #include "pviews/canvas.h"   // RectF/Paint/Path shared with the T2 API
//   #include "pviews/canvas_record.h"  // RenderGraph (replay target)
//   enum class ReplayErrorCode { NONE=0, EMPTY_PICTURE=1, BAD_SCALE=2,
//                                TOLERANCE_EXCEEDED=3 };
//   struct ReplayResult { bool ok; float maxPixelDelta;
//                         ReplayErrorCode error; };
//   inline constexpr float kTestGoldenTolerance = 1.0f;  // TEST-GOLDEN
//     // maxPixelDelta unit: absolute 8-bit channel distance per pixel.
//   class Picture {
//     RecordStatus drawRect(RectF r, const Paint& p) noexcept;
//     RecordStatus drawPath(const Path& path, const Paint& p) noexcept;
//     bool empty() const noexcept;  // true when zero ops recorded
//   };
//   ReplayResult replayPicture(const Picture& pic, float scale,
//                              RenderGraph& graph) noexcept;
//   // Replay renders pic into graph at `scale` and compares against the
//   // CPU reference raster; passes iff maxPixelDelta <= kTestGoldenTolerance.
//   // RenderGraph exposes the replayed raster for the 1x-vs-2x proof:
//   //   uint32_t replayWidth() const noexcept;
//   //   uint32_t replayHeight() const noexcept;
//   //   float replayPixelAt(uint32_t x, uint32_t y) const noexcept;
//   //     // grayscale channel value in [0,255] of the last replay.
//   // Scales <= 0, non-finite scales, and empty pictures are typed denials.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "pviews/picture.h"

static RectF validRect() { return RectF{0.0f, 0.0f, 10.0f, 10.0f}; }
static Paint validPaint() { return Paint{}; }

static Picture makeSamplePicture() {
  Picture pic;
  Path path;
  path.empty = false;
  pic.drawRect(validRect(), validPaint());
  pic.drawPath(path, validPaint());
  return pic;
}

// Case: replay at 1x passes within TEST-GOLDEN tolerance.
static bool check_replay_1x_within_tolerance() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph graph;
    const ReplayResult r = replayPicture(pic, 1.0f, graph);
    return r.ok && r.error == ReplayErrorCode::NONE &&
           r.maxPixelDelta >= 0.0f &&
           r.maxPixelDelta <= kTestGoldenTolerance;
  } catch (...) {
    return false;
  }
}

// Case: replay at 2x passes within TEST-GOLDEN tolerance.
static bool check_replay_2x_within_tolerance() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph graph;
    const ReplayResult r = replayPicture(pic, 2.0f, graph);
    return r.ok && r.error == ReplayErrorCode::NONE &&
           r.maxPixelDelta >= 0.0f &&
           r.maxPixelDelta <= kTestGoldenTolerance;
  } catch (...) {
    return false;
  }
}

// Case: one Picture replayed at 1x and 2x is identical except scale —
// replay extents double and downsampled 2x pixels match 1x pixels within
// TEST-GOLDEN tolerance.
static bool check_replay_identical_except_scale() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph g1;
    RenderGraph g2;
    const ReplayResult r1 = replayPicture(pic, 1.0f, g1);
    const ReplayResult r2 = replayPicture(pic, 2.0f, g2);
    if (!(r1.ok && r2.ok)) return false;
    if (g2.replayWidth() != 2 * g1.replayWidth()) return false;
    if (g2.replayHeight() != 2 * g1.replayHeight()) return false;
    for (uint32_t y = 0; y < g1.replayHeight(); ++y) {
      for (uint32_t x = 0; x < g1.replayWidth(); ++x) {
        const float v1 = g1.replayPixelAt(x, y);
        const float v2 = g2.replayPixelAt(2 * x, 2 * y);
        if (std::fabs(v2 - v1) > kTestGoldenTolerance) return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Case: empty picture is a typed EMPTY_PICTURE denial.
static bool check_replay_empty_picture_denied() {
  try {
    Picture pic;  // zero recorded ops
    if (!pic.empty()) return false;
    RenderGraph graph;
    const ReplayResult r = replayPicture(pic, 1.0f, graph);
    return !r.ok && r.error == ReplayErrorCode::EMPTY_PICTURE;
  } catch (...) {
    return false;
  }
}

// Case: zero scale is a typed BAD_SCALE denial.
static bool check_replay_zero_scale_denied() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph graph;
    const ReplayResult r = replayPicture(pic, 0.0f, graph);
    return !r.ok && r.error == ReplayErrorCode::BAD_SCALE;
  } catch (...) {
    return false;
  }
}

// Case: negative scale is a typed BAD_SCALE denial.
static bool check_replay_negative_scale_denied() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph graph;
    const ReplayResult r = replayPicture(pic, -1.0f, graph);
    return !r.ok && r.error == ReplayErrorCode::BAD_SCALE;
  } catch (...) {
    return false;
  }
}

// Case: non-finite scale is a typed BAD_SCALE denial (never NaN-poisons
// the graph: result is a denial, not a silent pass).
static bool check_replay_nan_scale_denied() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph graph;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const ReplayResult r = replayPicture(pic, nan, graph);
    return !r.ok && r.error == ReplayErrorCode::BAD_SCALE;
  } catch (...) {
    return false;
  }
}

// Case: tolerance-exceeded shape — a replay denial carries a
// TOLERANCE_EXCEEDED code with maxPixelDelta above tolerance, while a pass
// carries NONE with maxPixelDelta at or below tolerance (consistency of
// the error contract, whichever path the reference takes).
static bool check_replay_tolerance_contract_consistent() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph graph;
    const ReplayResult r = replayPicture(pic, 1.0f, graph);
    if (r.ok) {
      return r.error == ReplayErrorCode::NONE &&
             r.maxPixelDelta <= kTestGoldenTolerance;
    }
    return r.error == ReplayErrorCode::TOLERANCE_EXCEEDED &&
           r.maxPixelDelta > kTestGoldenTolerance;
  } catch (...) {
    return false;
  }
}

// Case: replay is deterministic — the same picture at the same scale
// reports the same maxPixelDelta twice.
static bool check_replay_deterministic() {
  try {
    Picture pic = makeSamplePicture();
    RenderGraph g1;
    RenderGraph g2;
    const ReplayResult r1 = replayPicture(pic, 1.0f, g1);
    const ReplayResult r2 = replayPicture(pic, 1.0f, g2);
    if (!(r1.ok && r2.ok)) return false;
    return std::fabs(r1.maxPixelDelta - r2.maxPixelDelta) <= 1e-6f;
  } catch (...) {
    return false;
  }
}

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
  RUN_CASE(check_replay_1x_within_tolerance);
  RUN_CASE(check_replay_2x_within_tolerance);
  RUN_CASE(check_replay_identical_except_scale);
  RUN_CASE(check_replay_empty_picture_denied);
  RUN_CASE(check_replay_zero_scale_denied);
  RUN_CASE(check_replay_negative_scale_denied);
  RUN_CASE(check_replay_nan_scale_denied);
  RUN_CASE(check_replay_tolerance_contract_consistent);
  RUN_CASE(check_replay_deterministic);
  if (failures > 0) {
    std::printf("canvas_conformance_test: %d failing\n", failures);
    return 1;
  }
  std::printf("canvas_conformance_test: all passing\n");
  return 0;
}
