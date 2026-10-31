// T3 (FR-TEXT-012 / AC-TEXT-030): scale-aware incremental remeasure.
// Hermetic harness: plain C++17, no test framework, no device, no downloads.
// Cases drive only the public scale_layout interface with inline dummy nodes.
// Shape mirrors engine/text/tests/nonlinear_scale_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "muse/text/font_scale.h"
#include "muse/text/scale_layout.h"

// Plan Interface line (verbatim):
// RemeasureDirty(nodes: vector<TextNode>&, newFontScale: float, density:
// float): size_t — returns remeasure count; updates resolvedScale to
// NonlinearScale(newFontScale) and clears dirty only on remeasured nodes;
// error shape: none — density accepted for cache-key width derivation, does
// not change stale set; repeat same-scale pass returns 0.

namespace {

muse::text::TextNode cleanNode(const std::string& t, float sizePx) {
  muse::text::TextNode n;
  n.text = t;
  n.sizePx = sizePx;
  n.resolvedScale = 1.0f;  // resolved at fontScale 1.0 (identity).
  n.styleGen = 1u;
  n.dirty = false;
  return n;
}

}  // namespace

static bool check_empty_tree_remeasures_zero() {
  std::vector<muse::text::TextNode> nodes;
  if (muse::text::RemeasureDirty(nodes, 1.5f, 2.0f) != 0u) return false;
  return true;
}

static bool check_clean_unchanged_scale_remeasures_zero() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("para one", 14.0f));
  nodes.push_back(cleanNode("para two", 16.0f));
  // Same fontScale the nodes were resolved at: nothing to do.
  if (muse::text::RemeasureDirty(nodes, 1.0f, 2.0f) != 0u) return false;
  if (nodes[0].resolvedScale != 1.0f) return false;
  if (nodes[0].dirty) return false;
  return true;
}

static bool check_dirty_nodes_remeasured_and_cleared() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("dirty para", 14.0f));
  nodes[0].dirty = true;
  const size_t n = muse::text::RemeasureDirty(nodes, 1.0f, 2.0f);
  if (n != 1u) return false;
  if (nodes[0].dirty) return false;  // remeasured nodes come back clean
  return true;
}

static bool check_scale_change_remeasures_only_stale_nodes() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("stale", 14.0f));  // resolved 1.0, scale moves
  nodes.push_back(cleanNode("fresh", 14.0f));
  nodes[1].resolvedScale = 0.0f;  // placeholder, fixed below
  // Resolve the second node at the incoming scale first so only the first
  // node is stale. We do that by pre-running remeasure on a one-node slice.
  // ASSUMPTION: RemeasureDirty sets resolvedScale to NonlinearScale(newFontScale)
  // for every remeasured node (T2 curve reuse).
  std::vector<muse::text::TextNode> probe;
  probe.push_back(cleanNode("fresh", 14.0f));
  (void)muse::text::RemeasureDirty(probe, 1.5f, 2.0f);
  nodes[1].resolvedScale = probe[0].resolvedScale;
  const size_t n = muse::text::RemeasureDirty(nodes, 1.5f, 2.0f);
  if (n != 1u) return false;
  if (nodes[0].resolvedScale != nodes[1].resolvedScale) return false;
  if (nodes[0].dirty || nodes[1].dirty) return false;
  return true;
}

static bool check_second_pass_is_quiet() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("a", 14.0f));
  nodes.push_back(cleanNode("b", 14.0f));
  nodes[0].dirty = true;
  (void)muse::text::RemeasureDirty(nodes, 1.5f, 2.0f);
  // Converged state: a repeat call at the same scale remeasures nothing.
  if (muse::text::RemeasureDirty(nodes, 1.5f, 2.0f) != 0u) return false;
  return true;
}

static bool check_small_nudge_stays_clean_but_step_remeasures() {
  // Plan Detail: "small nudge stays clean within epsilon, large step
  // remeasures". 1.0 -> 1.01 is the canonical nudge; 1.0 -> 1.5 the step.
  std::vector<muse::text::TextNode> nudge;
  nudge.push_back(cleanNode("nudge", 14.0f));
  nudge.push_back(cleanNode("nudge2", 14.0f));
  if (muse::text::RemeasureDirty(nudge, 1.01f, 2.0f) != 0u) return false;
  if (nudge[0].resolvedScale != 1.0f) return false;  // untouched
  if (nudge[0].dirty || nudge[1].dirty) return false;

  std::vector<muse::text::TextNode> step;
  step.push_back(cleanNode("step", 14.0f));
  step.push_back(cleanNode("step2", 14.0f));
  if (muse::text::RemeasureDirty(step, 1.5f, 2.0f) != 2u) return false;
  const float target = muse::text::NonlinearScale(1.5f);
  if (step[0].resolvedScale != target) return false;
  if (step[1].resolvedScale != target) return false;
  return true;
}

static bool check_remeasured_scale_tracks_nonlinear_curve() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("curve", 14.0f));
  nodes[0].dirty = true;
  (void)muse::text::RemeasureDirty(nodes, 2.0f, 2.0f);
  if (nodes[0].resolvedScale != muse::text::NonlinearScale(2.0f)) return false;
  return true;
}

static bool check_density_does_not_change_stale_set() {
  // Interface: density is accepted for cache-key width derivation and does
  // not change which nodes are stale. Same scale, different densities must
  // agree on the remeasure count.
  std::vector<muse::text::TextNode> lo;
  lo.push_back(cleanNode("d", 14.0f));
  std::vector<muse::text::TextNode> hi;
  hi.push_back(cleanNode("d", 14.0f));
  const size_t nLo = muse::text::RemeasureDirty(lo, 1.5f, 1.0f);
  const size_t nHi = muse::text::RemeasureDirty(hi, 1.5f, 3.0f);
  if (nLo != nHi) return false;
  if (nLo != 1u) return false;
  if (lo[0].resolvedScale != hi[0].resolvedScale) return false;
  return true;
}

static bool check_text_size_and_stylegen_preserved_by_remeasure() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("keep me", 18.0f));
  nodes[0].styleGen = 42u;
  nodes[0].dirty = true;
  (void)muse::text::RemeasureDirty(nodes, 2.0f, 3.0f);
  if (nodes[0].text != "keep me") return false;
  if (nodes[0].sizePx != 18.0f) return false;
  if (nodes[0].styleGen != 42u) return false;
  return true;
}

static bool check_skipped_nodes_keep_flags_and_scale() {
  // Only remeasured nodes get resolvedScale updated and dirty cleared;
  // clean converged nodes are byte-untouched.
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("stale", 14.0f));
  nodes.push_back(cleanNode("fresh", 14.0f));
  nodes[0].dirty = true;
  nodes[0].styleGen = 9u;
  std::vector<muse::text::TextNode> probe;
  probe.push_back(cleanNode("fresh", 14.0f));
  (void)muse::text::RemeasureDirty(probe, 1.5f, 2.0f);
  nodes[1].resolvedScale = probe[0].resolvedScale;
  nodes[1].styleGen = 7u;
  const size_t n = muse::text::RemeasureDirty(nodes, 1.5f, 2.0f);
  if (n != 1u) return false;
  if (nodes[0].styleGen != 9u) return false;
  if (nodes[1].styleGen != 7u) return false;
  if (nodes[1].resolvedScale != probe[0].resolvedScale) return false;
  return true;
}

static bool check_empty_text_node_remeasured_without_crash() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("", 14.0f));
  nodes[0].dirty = true;
  if (muse::text::RemeasureDirty(nodes, 1.5f, 2.0f) != 1u) return false;
  return true;
}

static bool check_out_of_range_scale_stays_finite() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("clamp", 14.0f));
  nodes[0].dirty = true;
  // ASSUMPTION: out-of-range newFontScale clamps to the T2 endpoint
  // multipliers (same policy as NonlinearScale); never NaN/inf, and the
  // node still counts as remeasured exactly once.
  if (muse::text::RemeasureDirty(nodes, 99.0f, 2.0f) != 1u) return false;
  const float r = nodes[0].resolvedScale;
  if (!(r == r)) return false;  // not NaN
  if (!(r > 0.0f && r < 100.0f)) return false;
  return true;
}

static bool check_signatures_match_plan() {
  static_assert(std::is_same_v<decltype(&muse::text::RemeasureDirty),
                               size_t (*)(std::vector<muse::text::TextNode>&,
                                           float, float)>);
  muse::text::TextNode n;
  n.text = "x";
  n.sizePx = 12.0f;
  n.resolvedScale = 1.0f;
  n.styleGen = 1u;
  n.dirty = false;
  static_assert(std::is_same_v<decltype(n.text), std::string>);
  static_assert(std::is_same_v<decltype(n.sizePx), float>);
  static_assert(std::is_same_v<decltype(n.resolvedScale), float>);
  static_assert(std::is_same_v<decltype(n.styleGen), uint64_t>);
  static_assert(std::is_same_v<decltype(n.dirty), bool>);
  (void)n;
  return true;
}

#define RUN(c)                                        \
  do {                                                \
    if (!c()) {                                       \
      std::printf("FAIL %s\n", #c);                   \
      failures++;                                     \
    } else {                                          \
      std::printf("PASS %s\n", #c);                   \
    }                                                 \
  } while (0)

int main() {
  int failures = 0;
  RUN(check_empty_tree_remeasures_zero);
  RUN(check_clean_unchanged_scale_remeasures_zero);
  RUN(check_dirty_nodes_remeasured_and_cleared);
  RUN(check_scale_change_remeasures_only_stale_nodes);
  RUN(check_second_pass_is_quiet);
  RUN(check_small_nudge_stays_clean_but_step_remeasures);
  RUN(check_remeasured_scale_tracks_nonlinear_curve);
  RUN(check_density_does_not_change_stale_set);
  RUN(check_text_size_and_stylegen_preserved_by_remeasure);
  RUN(check_skipped_nodes_keep_flags_and_scale);
  RUN(check_empty_text_node_remeasured_without_crash);
  RUN(check_out_of_range_scale_stays_finite);
  RUN(check_signatures_match_plan);
  if (failures == 0) std::printf("scale_remeasure_test: all passing\n");
  return failures == 0 ? 0 : 1;
}
