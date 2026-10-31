// T2 contract scaffold tests — FR-PVIEWS-007 (no AC in source; verified via T4/T5).
// Binds ONLY to the plan T2 `Interface:` line; never to implementation.
// Hermetic: C++17 + pviews/canvas.h (to-be-created) only; no Vulkan device,
// no render graph, no JNI/Java Canvas, no network, no env/config reads,
// no filesystem, no live services. Errors are return values, never thrown.
//
// RED state (Batch 2, pre-coder): pviews/canvas.h and src/canvas.cpp do not
// exist yet, so this file does NOT compile. That compile failure is the
// expected red for every case below. Batch 3 (T4) extends this scaffold
// with recordCanvas/graph/budget assertions; Batch 3 (T5) adds conformance/
// Picture tests in canvas_conformance_test.cpp. This file MUST NOT gain
// recordCanvas, RenderGraph/FrameBudget, Picture, or conformance assertions
// (T4/T5-owned).
//
// ASSUMED header shape (see unit-tests/T2.md; coder must satisfy it;
// everything beyond the `Interface:` line is marked ASSUMPTION there):
//   struct RectF { float left, top, right, bottom; };
//   struct Paint { bool valid (default true); };          // false == BAD_PAINT
//   struct Path { bool empty (default false); };          // true == EMPTY_PATH
//   enum class CanvasErrorCode { NONE=0, BAD_PAINT=1, EMPTY_PATH=2,
//                                CLIP_EMPTY=3, RECORD_FULL=4 };
//   struct RecordStatus { bool ok; CanvasErrorCode error; };
//   class Canvas {
//     RecordStatus drawRect(RectF r, const Paint& p);
//     RecordStatus drawPath(const Path& path, const Paint& p);
//     int save();
//     RecordStatus restore();
//     RecordStatus clipRect(RectF r);
//   };
// Empty-rect probe: a rect with right <= left or bottom <= top is empty
// (CLIP_EMPTY for clipRect; drawRect treats empty rect as ok/no-op).
#include <cstdio>

#include "pviews/canvas.h"

static RectF validRect() { return RectF{0.0f, 0.0f, 10.0f, 10.0f}; }
static RectF emptyRect() { return RectF{5.0f, 5.0f, 5.0f, 5.0f}; }
static Paint validPaint() { return Paint{}; }

// Case: drawRect with a valid rect + valid paint records ok + NONE.
static bool check_draw_rect_ok() {
  Canvas canvas;
  try {
    const RecordStatus r = canvas.drawRect(validRect(), validPaint());
    return r.ok && r.error == CanvasErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: drawRect with an invalid paint is a typed BAD_PAINT denial.
static bool check_draw_rect_bad_paint() {
  Canvas canvas;
  Paint bad;
  bad.valid = false;
  try {
    const RecordStatus r = canvas.drawRect(validRect(), bad);
    return !r.ok && r.error == CanvasErrorCode::BAD_PAINT;
  } catch (...) {
    return false;
  }
}

// Case: drawPath with a non-empty path records ok + NONE.
static bool check_draw_path_ok() {
  Canvas canvas;
  Path path;
  path.empty = false;
  try {
    const RecordStatus r = canvas.drawPath(path, validPaint());
    return r.ok && r.error == CanvasErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: drawPath with an empty path is a typed EMPTY_PATH denial.
static bool check_draw_path_empty_denied() {
  Canvas canvas;
  Path path;
  path.empty = true;
  try {
    const RecordStatus r = canvas.drawPath(path, validPaint());
    return !r.ok && r.error == CanvasErrorCode::EMPTY_PATH;
  } catch (...) {
    return false;
  }
}

// Case: save returns a depth token and restore after save reports ok.
static bool check_save_restore_ok() {
  Canvas canvas;
  try {
    const int depth = canvas.save();
    if (depth < 1) return false;
    const RecordStatus r = canvas.restore();
    return r.ok && r.error == CanvasErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: nested save/save/restore/restore keeps the stack balanced (ok).
static bool check_nested_save_restore_ok() {
  Canvas canvas;
  try {
    const int d1 = canvas.save();
    const int d2 = canvas.save();
    if (!(d2 > d1)) return false;
    const RecordStatus r1 = canvas.restore();
    const RecordStatus r2 = canvas.restore();
    return r1.ok && r2.ok;
  } catch (...) {
    return false;
  }
}

// Case: clipRect with a non-empty rect records ok + NONE.
static bool check_clip_rect_ok() {
  Canvas canvas;
  try {
    const RecordStatus r = canvas.clipRect(validRect());
    return r.ok && r.error == CanvasErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: clipRect with an empty rect is a typed CLIP_EMPTY denial.
static bool check_clip_rect_empty_denied() {
  Canvas canvas;
  try {
    const RecordStatus r = canvas.clipRect(emptyRect());
    return !r.ok && r.error == CanvasErrorCode::CLIP_EMPTY;
  } catch (...) {
    return false;
  }
}

// Case (ordering scaffold): the T2 sequence drawRect/drawPath + save/clip/
// restore records each op in order with zero synchronous flushes. At the T2
// boundary there is no op-list or flush-counter accessor, so this scaffold
// asserts the observable prefix: every call in the sequence is accepted
// (ok/NONE, save depth strictly increasing). Full ordering + never-flush
// proof moves to the T4 record test (recordCanvas exposes the graph op list
// and the no-flush scheduling assertion) in Batch 3.
static bool check_record_sequence_accepted_in_order() {
  Canvas canvas;
  Path path;
  path.empty = false;
  try {
    RecordStatus r = canvas.drawRect(validRect(), validPaint());
    if (!(r.ok && r.error == CanvasErrorCode::NONE)) return false;
    r = canvas.drawPath(path, validPaint());
    if (!(r.ok && r.error == CanvasErrorCode::NONE)) return false;
    const int depth = canvas.save();
    if (depth < 1) return false;
    r = canvas.clipRect(validRect());
    if (!(r.ok && r.error == CanvasErrorCode::NONE)) return false;
    r = canvas.restore();
    return r.ok && r.error == CanvasErrorCode::NONE;
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
  RUN_CASE(check_draw_rect_ok);
  RUN_CASE(check_draw_rect_bad_paint);
  RUN_CASE(check_draw_path_ok);
  RUN_CASE(check_draw_path_empty_denied);
  RUN_CASE(check_save_restore_ok);
  RUN_CASE(check_nested_save_restore_ok);
  RUN_CASE(check_clip_rect_ok);
  RUN_CASE(check_clip_rect_empty_denied);
  RUN_CASE(check_record_sequence_accepted_in_order);
  if (failures > 0) {
    std::printf("canvas_record_test: %d failing\n", failures);
    return 1;
  }
  std::printf("canvas_record_test: all passing\n");
  return 0;
}
