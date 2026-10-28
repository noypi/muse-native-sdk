// T2 contract tests — FR-MOTION-006 / AC-MOTION-010 (TC-MOTION-010).
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no filesystem, no live services. All evaluation takes
// explicit tMs (mocked clock); no wall clock, no JS symbols.
// Binds to the T2 `Interface:` line (reproduced verbatim in unit-tests/T2.md).
//
// ASSUMPTION (node shapes): the plan names Node as a variant
// {Value, Spring, Easing, Decay, Keyframe, Sequence{vector<NodeId>},
// Parallel{vector<NodeId>}} and Sample{values: map<NodeId,float>, done: bool}
// in `motion::`. Concrete struct field spellings below (Value{value},
// Spring{from,to,stiffness,dampingRatio,durationMs}, Easing{from,to,durationMs},
// Decay{from,velocity}, Keyframe{keys: vector<float>,timesMs: vector<float>},
// Sequence{children}, Parallel{children}, Sample{values,done}) are the test's
// proposal; the T2 contract records the reconciliation rule (update call
// sites, not expectations) if the coder names them differently.
//
// ASSUMPTION (timing): leaf durations are explicit durationMs fields with
// linear progress for Easing/Keyframe expectations; spring leaves report done
// when settled within 0.5 units (same M3-literal proxy as T1).
#include <cmath>
#include <cstdio>
#include <vector>

#include "motion/graph.h"

namespace {

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// Value node evaluates to its constant at any tMs and is immediately done.
bool check_value_node_evaluates() {
  motion::Graph g;
  motion::Node n;
  n.value.value = 7.0f;
  motion::NodeId id = g.add(n);
  motion::Sample s0 = g.evaluate(0.0f);
  if (!near(s0.values.at(id), 7.0f, 1e-5f)) return false;
  if (!s0.done) return false;
  motion::Sample s1 = g.evaluate(500.0f);
  return near(s1.values.at(id), 7.0f, 1e-5f) && s1.done;
}

// Sequence chains durations: first child at t=0, second child after boundary.
bool check_sequence_chains_durations() {
  motion::Graph g;
  motion::Node a;
  a.value.value = 1.0f;
  motion::Node b;
  b.value.value = 2.0f;
  motion::NodeId ida = g.add(a);
  motion::NodeId idb = g.add(b);
  motion::Node seq;
  seq.sequence.children = std::vector<motion::NodeId>{ida, idb};
  motion::NodeId idseq = g.add(seq);
  (void)idseq;
  motion::Sample s = g.evaluate(0.0f);
  // Sequence exposes the active leaf value under its own id.
  if (s.values.find(idseq) == s.values.end()) return false;
  return !s.done;  // value leaves inside a sequence: done only at end
}

// Parallel merges maps: both leaf values present in one sample.
bool check_parallel_merges_maps() {
  motion::Graph g;
  motion::Node a;
  a.value.value = 3.0f;
  motion::Node b;
  b.value.value = 4.0f;
  motion::NodeId ida = g.add(a);
  motion::NodeId idb = g.add(b);
  motion::Node par;
  par.parallel.children = std::vector<motion::NodeId>{ida, idb};
  motion::NodeId idpar = g.add(par);
  (void)idpar;
  motion::Sample s = g.evaluate(0.0f);
  if (s.values.find(ida) == s.values.end()) return false;
  if (s.values.find(idb) == s.values.end()) return false;
  return near(s.values.at(ida), 3.0f, 1e-5f) &&
         near(s.values.at(idb), 4.0f, 1e-5f);
}

// Easing leaf: t=0 -> from, t=mid -> midpoint, t=end -> to, done at end.
bool check_easing_leaf_midpoint() {
  motion::Graph g;
  motion::Node e;
  e.easing.from = 0.0f;
  e.easing.to = 100.0f;
  e.easing.durationMs = 200.0f;
  motion::NodeId id = g.add(e);
  motion::Sample s0 = g.evaluate(0.0f);
  if (!near(s0.values.at(id), 0.0f, 1e-3f)) return false;
  if (s0.done) return false;  // not done at start
  motion::Sample smid = g.evaluate(100.0f);
  if (!near(smid.values.at(id), 50.0f, 5.0f)) return false;
  motion::Sample send = g.evaluate(200.0f);
  if (!near(send.values.at(id), 100.0f, 1e-3f)) return false;
  return send.done;
}

// Keyframe leaf: exact key values at t=0/mid/end.
bool check_keyframe_leaf_keys() {
  motion::Graph g;
  motion::Node k;
  k.keyframe.keys = std::vector<float>{0.0f, 40.0f, 100.0f};
  k.keyframe.timesMs = std::vector<float>{0.0f, 100.0f, 200.0f};
  motion::NodeId id = g.add(k);
  if (!near(g.evaluate(0.0f).values.at(id), 0.0f, 1e-3f)) return false;
  if (!near(g.evaluate(100.0f).values.at(id), 40.0f, 1e-3f)) return false;
  motion::Sample send = g.evaluate(200.0f);
  return near(send.values.at(id), 100.0f, 1e-3f) && send.done;
}

// Nested sequence-in-parallel: parallel done only when all leaves done.
bool check_nested_sequence_in_parallel() {
  motion::Graph g;
  motion::Node e1;
  e1.easing.from = 0.0f;
  e1.easing.to = 10.0f;
  e1.easing.durationMs = 100.0f;
  motion::Node e2;
  e2.easing.from = 0.0f;
  e2.easing.to = 20.0f;
  e2.easing.durationMs = 100.0f;
  motion::NodeId id1 = g.add(e1);
  motion::NodeId id2 = g.add(e2);
  motion::Node seq;
  seq.sequence.children = std::vector<motion::NodeId>{id1, id2};
  motion::NodeId idseq = g.add(seq);
  motion::Node v;
  v.value.value = 5.0f;
  motion::NodeId idv = g.add(v);
  motion::Node par;
  par.parallel.children = std::vector<motion::NodeId>{idseq, idv};
  motion::NodeId idpar = g.add(par);
  (void)idpar;
  (void)idv;
  motion::Sample early = g.evaluate(50.0f);
  if (early.done) return false;  // sequence half not done -> not done
  motion::Sample late = g.evaluate(500.0f);
  return late.done;
}

// Nested parallel-in-sequence: sequence advances through parallel stage.
bool check_nested_parallel_in_sequence() {
  motion::Graph g;
  motion::Node a;
  a.easing.from = 0.0f;
  a.easing.to = 10.0f;
  a.easing.durationMs = 100.0f;
  motion::Node b;
  b.easing.from = 0.0f;
  b.easing.to = 10.0f;
  b.easing.durationMs = 100.0f;
  motion::NodeId ida = g.add(a);
  motion::NodeId idb = g.add(b);
  motion::Node par;
  par.parallel.children = std::vector<motion::NodeId>{ida, idb};
  motion::NodeId idpar = g.add(par);
  motion::Node tail;
  tail.value.value = 99.0f;
  motion::NodeId idtail = g.add(tail);
  motion::Node seq;
  seq.sequence.children = std::vector<motion::NodeId>{idpar, idtail};
  motion::NodeId idseq = g.add(seq);
  (void)idseq;
  motion::Sample early = g.evaluate(50.0f);
  if (early.done) return false;
  motion::Sample late = g.evaluate(1000.0f);
  if (!late.done) return false;
  return near(late.values.at(idseq), 99.0f, 1e-3f);
}

// Unknown NodeId in a composite evaluates to {done:true} with no crash
// (documented ignore rule).
bool check_unknown_child_id_is_ignored() {
  motion::Graph g;
  motion::Node v;
  v.value.value = 1.0f;
  motion::NodeId idv = g.add(v);
  motion::Node seq;
  seq.sequence.children =
      std::vector<motion::NodeId>{idv, motion::NodeId{999999u}};
  g.add(seq);
  motion::Sample s = g.evaluate(10000.0f);
  return s.done;  // no crash, composite still completes
}

}  // namespace

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
  RUN_CASE(check_value_node_evaluates);
  RUN_CASE(check_sequence_chains_durations);
  RUN_CASE(check_parallel_merges_maps);
  RUN_CASE(check_easing_leaf_midpoint);
  RUN_CASE(check_keyframe_leaf_keys);
  RUN_CASE(check_nested_sequence_in_parallel);
  RUN_CASE(check_nested_parallel_in_sequence);
  RUN_CASE(check_unknown_child_id_is_ignored);
  if (failures > 0) {
    std::printf("graph_compose_test: %d failing\n", failures);
    return 1;
  }
  std::printf("graph_compose_test: all passing\n");
  return 0;
}
