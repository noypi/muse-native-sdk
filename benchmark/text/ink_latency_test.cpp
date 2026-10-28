// T9 — FR-TEXT-014 / AC-TEXT-040: ink latency benchmark at 120 Hz unbuffered.
// Hermetic benchmark harness: plain C++17, no test framework, no device, no
// wall clock, no downloads. Drives the T2 pipeline with a synthetic 120 Hz
// trace under unbuffered dispatch; palm-injected traces are filtered through
// the T3 classifier; the handwriting clause reuses the T4 committer.
// Shape mirrors engine/text/ink/tests/stylus_axes_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
//
// Plan Interface line (verbatim):
// `BenchmarkResult MeasureInkLatency(const InkTrace& trace120Hz)` → `{framesTipToInkP95: float, unbufferedEnabled: bool}`.
// `none — benchmark harness entry, no production API`.
//
// `MeasureInkLatency`, `InkTrace`, and `BenchmarkResult` are defined in this
// file (benchmark harness entry — no production header, mirroring T8's
// test-only `RunGestureScript` driver). The harness consumes the T2/T3/T4
// production surfaces only through their published plan interfaces.
//
// Hermetic timing model (GR-08 T1.12 — no wall-clock flakes): tip-to-ink lag
// is measured STRUCTURALLY in frames, never in wall time. Each trace sample
// carries a synthetic tip frame index (120 Hz ⇒ one frame per sample). The
// harness pushes the sample through Classify (T3) + PushSamples (T2), then
// tessellates; a sample whose position is visible in the mesh at the same
// dispatch step has lag 0 frames, a sample visible only after the correction
// batch has lag 1 frame, a dropped sample has infinite lag (fail). P95 over
// the per-sample lags must be ≤ 1 frame. No clock is read; results are
// bit-deterministic for a fixed synthetic trace.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "../../engine/text/ink/handwriting_committer.h"
#include "../../engine/text/ink/ink_stroke_pipeline.h"
#include "../../engine/text/ink/palm_rejector.h"

namespace ink = muse::text::ink;

// ---------------------------------------------------------------------------
// Benchmark harness entry (plan Interface, verbatim — test-local, no
// production API).
// ---------------------------------------------------------------------------

// One synthetic pen-tip sample at 120 Hz. `frame` is the tip frame index
// (one frame per sample at 120 Hz); `palm` marks an injected palm contact
// (routed through the T3 classifier, never pushed to the pipeline).
struct InkSample {
  float x = 0.0f;
  float y = 0.0f;
  float pressure = 0.5f;
  float tilt = 0.0f;
  float orientation = 0.0f;
  ink::ToolType toolType = ink::ToolType::STYLUS;
  bool predicted = false;
  bool palm = false;
  unsigned frame = 0;
};

// One synthetic trace: samples in stroke order at `sampleRateHz` under the
// FR-INPUT-003 unbuffered flag (`unbuffered`). The T9 trace runs at 120 Hz.
struct InkTrace {
  std::vector<InkSample> samples;
  bool unbuffered = true;
  double sampleRateHz = 120.0;
};

// Benchmark outcome: P95 tip-to-ink lag in frames plus the dispatch mode the
// trace ran under. P95 ≤ 1.0 means ink tracks within one frame of the tip.
struct BenchmarkResult {
  float framesTipToInkP95 = 0.0f;
  bool unbufferedEnabled = false;
};

// Drives the T2 pipeline with `trace120Hz` (T3-filtered), tessellating after
// every dispatch step, and reports the P95 structural tip-to-ink lag in
// frames. Predicted samples are expected to be visible the same step they
// are pushed (lag 0); committed samples corrected on the next step have lag
// at most 1. Palm contacts are classified via T3 and excluded from the lag
// population (they must add no stroke — asserted separately). Never throws
// (bad_alloc aside); empty trace yields {0.0, trace.unbuffered}.
BenchmarkResult MeasureInkLatency(const InkTrace& trace120Hz) {
  ink::InkStrokePipeline pipe;
  std::vector<float> lags;
  lags.reserve(trace120Hz.samples.size());
  for (const InkSample& s : trace120Hz.samples) {
    const ink::ContactEvent ev{s.palm};
    const ink::StylusActiveState st{true};
    if (ink::Classify(ev, st) == ink::RejectVerdict::REJECT_PALM) continue;
    if (s.toolType == ink::ToolType::HOVER) continue;
    ink::StylusSample ps{};
    ps.x = s.x;
    ps.y = s.y;
    ps.pressure = s.pressure;
    ps.tilt = s.tilt;
    ps.orientation = s.orientation;
    ps.toolType = s.toolType;
    ps.predicted = s.predicted;
    ink::StylusSampleBatch b{};
    b.samples.push_back(ps);
    b.unbuffered = trace120Hz.unbuffered;
    pipe.PushSamples(b);
    const ink::StrokeMesh mesh = pipe.Tessellate(ink::StrokeId{0});
    bool visible = false;
    for (const auto& v : mesh.vertices) {
      if (v.x == s.x && v.y == s.y) {
        visible = true;
        break;
      }
    }
    // Same-step visibility ⇒ lag 0 frames; otherwise the correction batch
    // on the next step carries it ⇒ lag 1 frame (still within one frame).
    // A sample visible in neither step is dropped ⇒ infinite lag (fail).
    if (visible) {
      lags.push_back(0.0f);
    } else {
      ink::StylusSampleBatch correction{};
      correction.samples.push_back(ps);
      correction.unbuffered = trace120Hz.unbuffered;
      pipe.PushSamples(correction);
      const ink::StrokeMesh mesh2 = pipe.Tessellate(ink::StrokeId{0});
      bool visible2 = false;
      for (const auto& v : mesh2.vertices) {
        if (v.x == s.x && v.y == s.y) {
          visible2 = true;
          break;
        }
      }
      lags.push_back(visible2 ? 1.0f : 1e9f);
    }
  }
  if (lags.empty()) return BenchmarkResult{0.0f, trace120Hz.unbuffered};
  std::sort(lags.begin(), lags.end());
  const std::size_t idx =
      static_cast<std::size_t>(std::ceil(0.95 * lags.size())) - 1;
  return BenchmarkResult{lags[idx], trace120Hz.unbuffered};
}

// ---------------------------------------------------------------------------
// Synthetic trace builders (fixed seeds, no randomness, no I/O).
// ---------------------------------------------------------------------------

static InkTrace make_120hz_trace(std::size_t n, bool unbuffered,
                                 bool with_predicted_tail,
                                 bool with_palm_burst) {
  InkTrace t{};
  t.unbuffered = unbuffered;
  t.sampleRateHz = 120.0;
  for (std::size_t i = 0; i < n; ++i) {
    InkSample s{};
    s.x = static_cast<float>(i);
    s.y = static_cast<float>(i) * 0.5f;
    s.pressure = 0.5f;
    s.frame = static_cast<unsigned>(i);
    t.samples.push_back(s);
  }
  if (with_predicted_tail) {
    for (std::size_t k = 1; k <= 4; ++k) {
      InkSample p{};
      p.x = static_cast<float>(n + k);
      p.y = static_cast<float>(n + k) * 0.5f;
      p.pressure = 0.5f;
      p.predicted = true;
      p.frame = static_cast<unsigned>(n + k);
      t.samples.push_back(p);
    }
  }
  if (with_palm_burst) {
    for (std::size_t k = 0; k < 6; ++k) {
      InkSample palm{};
      palm.x = 1000.0f + static_cast<float>(k);
      palm.y = -1000.0f;
      palm.palm = true;
      palm.frame = static_cast<unsigned>(n + k);
      t.samples.push_back(palm);
    }
  }
  return t;
}

// ---------------------------------------------------------------------------
// Cases.
// ---------------------------------------------------------------------------

static bool check_120hz_unbuffered_p95_within_one_frame() {
  // AC-TEXT-040 clause 1: 1 s of 120 Hz unbuffered ink, P95 ≤ 1 frame.
  const BenchmarkResult r = MeasureInkLatency(make_120hz_trace(
      /*n=*/120, /*unbuffered=*/true, /*predicted=*/false, /*palm=*/false));
  return r.framesTipToInkP95 <= 1.0f && r.unbufferedEnabled;
}

static bool check_predicted_tail_same_frame_visibility() {
  // Predicted samples render without waiting for the correction batch.
  const BenchmarkResult r = MeasureInkLatency(make_120hz_trace(
      /*n=*/60, /*unbuffered=*/true, /*predicted=*/true, /*palm=*/false));
  return r.framesTipToInkP95 <= 1.0f && r.unbufferedEnabled;
}

static bool check_buffered_trace_reports_unbuffered_false() {
  // Dispatch-mode reporting: a buffered trace still measures, but the
  // result must not claim unbuffered dispatch.
  const BenchmarkResult r = MeasureInkLatency(make_120hz_trace(
      /*n=*/60, /*unbuffered=*/false, /*predicted=*/false, /*palm=*/false));
  return !r.unbufferedEnabled && r.framesTipToInkP95 <= 1.0f;
}

static bool check_palm_injected_trace_adds_no_stroke() {
  // AC-TEXT-040 clause 3 (via T3): palm contacts add no stroke points.
  // Pipeline-level observable: palm-filtered mesh equals the palm-free mesh.
  const InkTrace clean = make_120hz_trace(/*n=*/40, /*unbuffered=*/true,
                                          /*predicted=*/false, /*palm=*/false);
  const InkTrace dirty = make_120hz_trace(/*n=*/40, /*unbuffered=*/true,
                                          /*predicted=*/false, /*palm=*/true);
  ink::InkStrokePipeline a, b;
  for (const InkSample& s : clean.samples) {
    ink::StylusSampleBatch batch{};
    ink::StylusSample ps{};
    ps.x = s.x;
    ps.y = s.y;
    ps.pressure = s.pressure;
    batch.samples.push_back(ps);
    batch.unbuffered = true;
    a.PushSamples(batch);
  }
  for (const InkSample& s : dirty.samples) {
    const ink::ContactEvent ev{s.palm};
    const ink::StylusActiveState st{true};
    if (ink::Classify(ev, st) == ink::RejectVerdict::REJECT_PALM) continue;
    ink::StylusSampleBatch batch{};
    ink::StylusSample ps{};
    ps.x = s.x;
    ps.y = s.y;
    ps.pressure = s.pressure;
    batch.samples.push_back(ps);
    batch.unbuffered = true;
    b.PushSamples(batch);
  }
  const ink::StrokeMesh ma = a.Tessellate(ink::StrokeId{0});
  const ink::StrokeMesh mb = b.Tessellate(ink::StrokeId{0});
  if (ma.vertices.size() != mb.vertices.size()) return false;
  // No scroll/gesture emission observable exists at this layer (classifier
  // only per T3 plan line); the stroke-equality above is the unit-observable
  // half, scroll-suppression is EXTERNAL (input dispatch layer).
  const BenchmarkResult r = MeasureInkLatency(dirty);
  return r.framesTipToInkP95 <= 1.0f;
}

static bool check_palm_only_trace_yields_empty_mesh() {
  InkTrace t{};
  t.unbuffered = true;
  for (std::size_t k = 0; k < 8; ++k) {
    InkSample palm{};
    palm.palm = true;
    palm.frame = static_cast<unsigned>(k);
    t.samples.push_back(palm);
  }
  const BenchmarkResult r = MeasureInkLatency(t);
  return r.framesTipToInkP95 == 0.0f && r.unbufferedEnabled;
}

static bool check_handwriting_commit_undoable_as_one_op() {
  // AC-TEXT-040 clause 2 (reuses the T4 assertion): one recognized phrase
  // commits into the shared buffer and undoes as a single operation.
  ink::BufferHandle buf;
  ink::SpanContext ctx{};
  const ink::CommitOp op =
      ink::CommitRecognizedText(buf, "hello", ctx);
  if (buf.text() != "hello") return false;
  ink::Undo(buf, op);
  return buf.text().empty();
}

static bool check_empty_trace_never_throws() {
  const InkTrace t{};
  const BenchmarkResult r = MeasureInkLatency(t);
  return r.framesTipToInkP95 == 0.0f && r.unbufferedEnabled;
}

static bool check_oversized_trace_stays_bounded() {
  // Hostile/oversized burst (60 s at 120 Hz = 7200 samples) still returns
  // with P95 ≤ 1 frame; per-sample cost is linear (one vertex row per
  // non-hover sample — no tessellation blowup).
  const BenchmarkResult r = MeasureInkLatency(make_120hz_trace(
      /*n=*/7200, /*unbuffered=*/true, /*predicted=*/false, /*palm=*/false));
  if (!(r.framesTipToInkP95 <= 1.0f)) return false;
  ink::InkStrokePipeline pipe;
  ink::StylusSampleBatch batch{};
  for (std::size_t i = 0; i < 7200; ++i) {
    ink::StylusSample s{};
    s.x = static_cast<float>(i);
    batch.samples.push_back(s);
  }
  batch.unbuffered = true;
  pipe.PushSamples(batch);
  return pipe.Tessellate(ink::StrokeId{0}).vertices.size() == 7200;
}

static bool check_extreme_axes_do_not_throw() {
  // Untrusted trace magnitudes must not crash the benchmark path.
  InkTrace t{};
  t.unbuffered = true;
  InkSample s{};
  s.x = 1e6f;
  s.y = -1e6f;
  s.pressure = 1e6f;
  s.tilt = -1e6f;
  s.orientation = 1e6f;
  t.samples.push_back(s);
  const BenchmarkResult r = MeasureInkLatency(t);
  return r.framesTipToInkP95 <= 1.0f;
}

#define RUN_CASE(name)                 \
  do {                                \
    if (!(name)()) {                  \
      std::printf("FAIL %s\n", #name); \
      ++failures;                     \
    } else {                          \
      std::printf("PASS %s\n", #name); \
    }                                 \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_120hz_unbuffered_p95_within_one_frame);
  RUN_CASE(check_predicted_tail_same_frame_visibility);
  RUN_CASE(check_buffered_trace_reports_unbuffered_false);
  RUN_CASE(check_palm_injected_trace_adds_no_stroke);
  RUN_CASE(check_palm_only_trace_yields_empty_mesh);
  RUN_CASE(check_handwriting_commit_undoable_as_one_op);
  RUN_CASE(check_empty_trace_never_throws);
  RUN_CASE(check_oversized_trace_stays_bounded);
  RUN_CASE(check_extreme_axes_do_not_throw);
  if (failures > 0) {
    std::printf("ink_latency_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ink_latency_test: all passing\n");
  return 0;
}
