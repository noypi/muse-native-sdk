// T2 scaffold + T4 record-into-render-graph tests — FR-PVIEWS-007 / FR-PVIEWS-014.
// Batch 2 scaffold (T2 cases) + Batch 3 extension (T4 cases, this batch).
// Binds ONLY to the plan T2/T4 `Interface:` lines; never to implementation.
// Hermetic: C++17 + pviews/canvas.h + pviews/canvas_record.h (to-be-created)
// only; no Vulkan device, no synchronous flush, no JNI/Java Canvas, no
// network, no env/config reads, no filesystem, no live services. Errors are
// return values, never thrown.
//
// RED state (Batch 3, pre-coder): pviews/canvas_record.h and
// src/canvas_record.cpp do not exist yet, so this file does NOT compile.
// That compile failure is the expected red for every T4 case below (the T2
// prefix compiles against the existing pviews/canvas.h).
//
// ASSUMED canvas_record.h shape (see unit-tests/T4.md; coder must satisfy
// it; everything beyond the `Interface:` line is marked ASSUMPTION there):
//   #include "pviews/canvas.h"
//   enum class RecordErrorCode { NONE=0, EMPTY_CANVAS=1,
//                                BUDGET_EXCEEDED=2, GRAPH_REJECTED=3 };
//   struct RecordResult { bool ok; uint32_t scheduledFrame;
//                         RecordErrorCode error; };
//   struct FrameBudget { uint32_t maxOps; uint32_t currentFrame; };
//     // maxOps: op capacity schedulable in currentFrame. A canvas whose op
//     // count fits schedules at currentFrame; overflow defers to
//     // currentFrame+1 with BUDGET_EXCEEDED and appends nothing.
//   enum class GraphOpKind { kDrawRect, kDrawPath, kSave, kRestore,
//                            kClipRect };
//   struct GraphOp { GraphOpKind kind; };
//   class RenderGraph {
//     size_t opCount() const noexcept;            // appended ops total
//     const GraphOp& opAt(size_t i) const;        // appended op in order
//     uint32_t flushCount() const noexcept;       // synchronous flushes
//     bool rejectNext = false;  // test hook: next append is GRAPH_REJECTED
//   };
//   RecordResult recordCanvas(const Canvas& canvas, RenderGraph& graph,
//                             FrameBudget budget) noexcept;
// ASSUMED Canvas cooperation: recordCanvas observes the T2 Canvas op list
// (friend or accessor — coder's choice); empty canvas == zero recorded ops;
// Canvas enforces its kMaxOps capacity with RECORD_FULL (T2 enum).
#include <cstdint>
#include <cstdio>

#include "pviews/canvas.h"
#include "pviews/canvas_record.h"

static RectF validRect() { return RectF{0.0f, 0.0f, 10.0f, 10.0f}; }
static RectF emptyRect() { return RectF{5.0f, 5.0f, 5.0f, 5.0f}; }
static Paint validPaint() { return Paint{}; }

// ---------------------------------------------------------------------------
// T2 scaffold cases (Batch 2, unchanged).
// ---------------------------------------------------------------------------

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
// proof is in the T4 record cases below (recordCanvas exposes the graph op
// list and the no-flush scheduling assertion).
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

// ---------------------------------------------------------------------------
// T4 record-into-render-graph cases (Batch 3, this batch).
// Spec refs: FR-PVIEWS-014, AC-PVIEWS-022; TC-PVIEWS-022
// (`./gradlew :pviews:canvasRecord` maps to CTest `canvas_record`).
// ---------------------------------------------------------------------------

static Canvas makeTwoOpCanvas() {
  Canvas canvas;
  Path path;
  path.empty = false;
  canvas.drawRect(validRect(), validPaint());
  canvas.drawPath(path, validPaint());
  return canvas;
}

static FrameBudget generousBudget() {
  FrameBudget b{};
  b.maxOps = 4096;
  b.currentFrame = 7;
  return b;
}

// Case: non-empty canvas schedules into the graph within budget.
static bool check_record_within_budget_schedules() {
  try {
    Canvas canvas = makeTwoOpCanvas();
    RenderGraph graph;
    const RecordResult r = recordCanvas(canvas, graph, generousBudget());
    return r.ok && r.error == RecordErrorCode::NONE &&
           r.scheduledFrame == 7 && graph.opCount() == 2 &&
           graph.flushCount() == 0;
  } catch (...) {
    return false;
  }
}

// Case: over-budget work defers with BUDGET_EXCEEDED + deferred
// scheduledFrame, appends nothing, and never blocks (no throw).
static bool check_record_over_budget_defers() {
  try {
    Canvas canvas = makeTwoOpCanvas();  // 2 ops
    RenderGraph graph;
    FrameBudget b{};
    b.maxOps = 1;  // strictly fewer than the canvas op count
    b.currentFrame = 7;
    const RecordResult r = recordCanvas(canvas, graph, b);
    return !r.ok && r.error == RecordErrorCode::BUDGET_EXCEEDED &&
           r.scheduledFrame == 8 && graph.opCount() == 0 &&
           graph.flushCount() == 0;
  } catch (...) {
    return false;
  }
}

// Case: empty canvas returns EMPTY_CANVAS and appends nothing.
static bool check_record_empty_canvas_denied() {
  try {
    Canvas canvas;  // zero recorded ops
    RenderGraph graph;
    const RecordResult r = recordCanvas(canvas, graph, generousBudget());
    return !r.ok && r.error == RecordErrorCode::EMPTY_CANVAS &&
           graph.opCount() == 0 && graph.flushCount() == 0;
  } catch (...) {
    return false;
  }
}

// Case: graph rejection surfaces GRAPH_REJECTED, never throws.
static bool check_record_graph_rejected() {
  try {
    Canvas canvas = makeTwoOpCanvas();
    RenderGraph graph;
    graph.rejectNext = true;
    const RecordResult r = recordCanvas(canvas, graph, generousBudget());
    return !r.ok && r.error == RecordErrorCode::GRAPH_REJECTED;
  } catch (...) {
    return false;
  }
}

// Case: full op-list ordering — drawRect/drawPath + save/clip/restore lands
// in the graph in recorded order.
static bool check_record_op_list_ordering() {
  try {
    Canvas canvas;
    Path path;
    path.empty = false;
    canvas.drawRect(validRect(), validPaint());
    canvas.drawPath(path, validPaint());
    canvas.save();
    canvas.clipRect(validRect());
    canvas.restore();
    RenderGraph graph;
    const RecordResult r = recordCanvas(canvas, graph, generousBudget());
    if (!(r.ok && r.error == RecordErrorCode::NONE)) return false;
    if (graph.opCount() != 5) return false;
    const GraphOpKind want[5] = {GraphOpKind::kDrawRect,
                                 GraphOpKind::kDrawPath, GraphOpKind::kSave,
                                 GraphOpKind::kClipRect,
                                 GraphOpKind::kRestore};
    for (std::size_t i = 0; i < 5; ++i) {
      if (graph.opAt(i).kind != want[i]) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Case: never-flush — scheduling performs zero synchronous flushes.
static bool check_record_never_flushes() {
  try {
    Canvas canvas = makeTwoOpCanvas();
    RenderGraph graph;
    const RecordResult r = recordCanvas(canvas, graph, generousBudget());
    return r.ok && graph.flushCount() == 0;
  } catch (...) {
    return false;
  }
}

// Case: RECORD_FULL overflow proof — filling the canvas past capacity is a
// typed RECORD_FULL denial (extra ops refused, earlier ops retained).
static bool check_record_full_overflow() {
  try {
    Canvas canvas;
    bool sawFull = false;
    for (int i = 0; i < 8192; ++i) {
      const RecordStatus s = canvas.drawRect(validRect(), validPaint());
      if (!s.ok) {
        if (s.error != CanvasErrorCode::RECORD_FULL) return false;
        sawFull = true;
        break;
      }
      if (s.error != CanvasErrorCode::NONE) return false;
    }
    return sawFull;
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
  RUN_CASE(check_record_within_budget_schedules);
  RUN_CASE(check_record_over_budget_defers);
  RUN_CASE(check_record_empty_canvas_denied);
  RUN_CASE(check_record_graph_rejected);
  RUN_CASE(check_record_op_list_ordering);
  RUN_CASE(check_record_never_flushes);
  RUN_CASE(check_record_full_overflow);
  if (failures > 0) {
    std::printf("canvas_record_test: %d failing\n", failures);
    return 1;
  }
  std::printf("canvas_record_test: all passing\n");
  return 0;
}
