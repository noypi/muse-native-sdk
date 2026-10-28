#pragma once

// FR-TEXT-014 / AC-TEXT-037: C++ ink stroke pipeline for stylus axes.
// Pure C++ entry point over the FR-INPUT-003 dispatch contract (historical +
// predicted batches, unbuffered flag). No JNI/JVM types appear here or on the
// per-sample path; the platform binding stays a thin layer above PushSamples.
// Sample axes mirror FR-TEXT-004 (engine/text stylus.h): position plus
// pressure/tilt/orientation, carried intact per sample into tessellation.
//
// Stroke model: one in-progress stroke (id 0). Hover samples never emit
// vertices; eraser samples tessellate like pen samples but flip the mesh
// erase flag. Predicted samples tessellate immediately (low-latency path)
// and are replaced — not duplicated — when the non-predicted correction
// batch arrives. The unbuffered flag never drops samples: buffered and
// unbuffered batches deliver every row. Never throws (bad_alloc aside).

#include <vector>

namespace muse::text::ink {

// Stylus tool type per sample. HOVER samples are position-only (no stroke);
// ERASER samples tessellate with erase semantics on the mesh.
enum class ToolType {
  STYLUS = 0,
  ERASER = 1,
  HOVER = 2,
};

// One stylus sample: position plus the ink-relevant axes. `tilt` here is the
// scalar tilt magnitude the ink stage consumes (FR-TEXT-004 carries the raw
// tiltX/tiltY pair; the dispatch layer folds it before PushSamples).
struct StylusSample {
  float x = 0.0f;
  float y = 0.0f;
  float pressure = 0.0f;
  float tilt = 0.0f;
  float orientation = 0.0f;
  ToolType toolType = ToolType::STYLUS;
  bool predicted = false;
};

// One dispatch batch: historical + predicted samples in stroke order, plus
// the FR-INPUT-003 unbuffered flag (delivery hint only — every sample
// reaches the stroke regardless of the flag).
struct StylusSampleBatch {
  std::vector<StylusSample> samples;
  bool unbuffered = false;
};

// Stroke handle. The first (and currently only tracked) stroke is id 0;
// Tessellate on any other id returns an empty mesh.
struct StrokeId {
  int value = 0;
};

// Tessellated vertex row: position plus variable width from pressure.
struct Vertex {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
};

// Tessellated stroke. `eraser` is true when the stroke was drawn with the
// eraser tool (erase semantics); otherwise it is a paint stroke.
struct StrokeMesh {
  std::vector<Vertex> vertices;
  bool eraser = false;
};

// Accumulates stylus batches into strokes and tessellates them with
// pressure-variable width. One vertex row per non-hover sample: rows are
// never merged or dropped, so tilt/orientation variations survive as
// distinct rows.
class InkStrokePipeline {
 public:
  InkStrokePipeline() = default;

  // Appends `batch` to the in-progress stroke. Hover samples are skipped;
  // predicted samples overlay until a non-predicted correction batch replaces
  // them. Pure C++; no JNI/JVM on the path. Never throws (bad_alloc aside).
  void PushSamples(const StylusSampleBatch& batch);

  // Tessellates stroke `id` (only id 0 carries data). Pure C++; no JNI/JVM
  // on the path. Never throws (bad_alloc aside).
  StrokeMesh Tessellate(StrokeId id) const;

 private:
  std::vector<StylusSample> committed_;
  std::vector<StylusSample> predicted_;
  bool saw_eraser_ = false;
};

}  // namespace muse::text::ink
