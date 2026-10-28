// T9 contract tests — FR-MOTION-006 / AC-MOTION-013 (proxy).
// 120Hz frame-budget assertion over the EXISTING T2 Graph::evaluate +
// T3 GestureBinding::onSample surfaces. No new production API.
//
// TC-MOTION-013 device-lab (motion-120hz) is UNVERIFIED-by-construction:
// a host unit test cannot prove on-device 120Hz frame pacing. This file is
// a hermetic proxy only: it measures host wall time for a nested
// sequence/parallel graph with springs+keyframes and asserts it fits the
// 8.33ms budget, and it asserts the drag write lands in the same frame
// (same tMs) as the sample. Device-lab validation remains open.
//
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Mocked clock/geometry for all values;
// host wall clock (std::chrono::steady_clock) is used ONLY as a stopwatch
// around evaluate/onSample — never as animation input.
//
// ASSUMPTION (threshold strategy): the binding assertion is the full
// 8.33ms budget per the plan acceptance ("evaluates < 8.33ms on host").
// Flakiness is contained by (a) a small fixed graph, (b) averaging over
// N=200 iterations, and (c) printf-reporting the measured mean/max so a
// marginal host can be diagnosed instead of retried blindly. The mean must
// clear the full budget; no tightened bound is asserted.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include "motion/gesture_binding.h"
#include "motion/graph.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

const float kFrameBudgetMs = 8.33f;  // 120Hz frame budget (specs-performance)
const int kIters = 200;

// A nested sequence-in-parallel graph with springs + keyframes, mirroring
// the TC-MOTION-010 composition shape at 120Hz scale.
motion::Graph buildBudgetGraph(motion::NodeId* outProbe) {
  motion::Graph g;
  motion::Node springLeaf;
  springLeaf.spring.from = 0.0f;
  springLeaf.spring.to = 100.0f;
  springLeaf.spring.stiffness = 180.0f;
  springLeaf.spring.dampingRatio = 1.0f;
  springLeaf.spring.durationMs = 500.0f;
  motion::Node keyLeaf;
  keyLeaf.keyframe.keys = std::vector<float>{0.0f, 40.0f, 100.0f};
  keyLeaf.keyframe.timesMs = std::vector<float>{0.0f, 250.0f, 500.0f};
  motion::Node easeLeaf;
  easeLeaf.easing.from = 0.0f;
  easeLeaf.easing.to = 50.0f;
  easeLeaf.easing.durationMs = 500.0f;
  motion::NodeId s = g.add(springLeaf);
  motion::NodeId k = g.add(keyLeaf);
  motion::NodeId e = g.add(easeLeaf);
  motion::Node seq;
  seq.sequence.children = std::vector<motion::NodeId>{s, k};
  motion::NodeId seqId = g.add(seq);
  motion::Node par;
  par.parallel.children = std::vector<motion::NodeId>{seqId, e};
  (void)g.add(par);
  *outProbe = e;
  return g;
}

class RecordingWriter : public motion::ITransformWriter {
 public:
  void writeTransform(float value, float tMs) override {
    lastValue = value;
    lastTMs = tMs;
    ++writes;
  }
  float lastValue = 0.0f;
  float lastTMs = -1.0f;
  int writes = 0;
};

// Nested graph evaluates within the 120Hz budget (mean over N iters).
// Reports mean/max; asserts mean < 8.33ms.
bool check_nested_graph_evaluates_within_budget() {
  motion::NodeId probe;
  motion::Graph g = buildBudgetGraph(&probe);
  // Warm up once so cold-cache effects do not dominate the mean.
  (void)g.evaluate(250.0f);
  auto t0 = std::chrono::steady_clock::now();
  double sink = 0.0;
  for (int i = 0; i < kIters; ++i) {
    motion::Sample s = g.evaluate(250.0f);
    sink += s.values.at(probe);
  }
  auto t1 = std::chrono::steady_clock::now();
  double totalMs =
      std::chrono::duration<double, std::milli>(t1 - t0).count();
  double meanMs = totalMs / kIters;
  // Peak single-evaluate cost (worst of a small probe sample).
  double worstMs = 0.0;
  for (int i = 0; i < 10; ++i) {
    auto a = std::chrono::steady_clock::now();
    motion::Sample s = g.evaluate(250.0f);
    sink += s.values.at(probe);
    auto b = std::chrono::steady_clock::now();
    double m = std::chrono::duration<double, std::milli>(b - a).count();
    if (m > worstMs) worstMs = m;
  }
  std::printf("  [budget] mean %.4f ms/evaluate, worst %.4f ms (N=%d)\n",
              meanMs, worstMs, kIters);
  if (sink == 123456789.0) std::printf("  [budget] %f\n", sink);  // no-op
  return meanMs < kFrameBudgetMs && worstMs < kFrameBudgetMs;
}

// Gesture drag sample -> writer write completes within the same budget.
bool check_drag_write_within_budget() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  motion::InputSample s;
  s.position = 42.0f;
  s.velocity = 1.5f;
  s.tMs = 100.0f;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kIters; ++i) {
    s.tMs = 100.0f + static_cast<float>(i);
    s.position = 42.0f + static_cast<float>(i) * 0.1f;
    binding.onSample(s);
  }
  auto t1 = std::chrono::steady_clock::now();
  double meanMs =
      std::chrono::duration<double, std::milli>(t1 - t0).count() / kIters;
  std::printf("  [budget] onSample mean %.4f ms (N=%d)\n", meanMs, kIters);
  return meanMs < kFrameBudgetMs;
}

// Drag write lands in the same frame: writer observes the sample's tMs.
bool check_drag_write_lands_in_same_frame() {
  RecordingWriter w;
  motion::GestureBinding binding(&w);
  motion::InputSample s;
  s.position = 60.0f;
  s.velocity = 2.0f;
  s.tMs = 833.0f;
  binding.onSample(s);
  if (w.writes != 1) return false;
  if (!near(w.lastTMs, 833.0f, 1e-5f)) return false;
  return near(w.lastValue, 60.0f, 1e-3f);
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_nested_graph_evaluates_within_budget);
  RUN_CASE(check_drag_write_within_budget);
  RUN_CASE(check_drag_write_lands_in_same_frame);
  if (failures > 0) {
    std::printf("frame_budget_test: %d failing\n", failures);
    return 1;
  }
  std::printf("frame_budget_test: all passing\n");
  return 0;
}
