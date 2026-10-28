// T5 (FR-TEXT-012 / AC-TEXT-030): scale-aware incremental remeasure.
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

#include "muse/text/scale_layout.h"

// Plan Interface line (verbatim):
// struct TextNode { std::string text; float sizePx; float resolvedScale;
// uint64_t styleGen; bool dirty; }; size_t RemeasureDirty(
// std::vector<TextNode>& nodes, float newFontScale, float density);

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

static bool check_text_and_size_preserved_by_remeasure() {
  std::vector<muse::text::TextNode> nodes;
  nodes.push_back(cleanNode("keep me", 18.0f));
  nodes[0].dirty = true;
  (void)muse::text::RemeasureDirty(nodes, 2.0f, 3.0f);
  if (nodes[0].text != "keep me") return false;
  if (nodes[0].sizePx != 18.0f) return false;
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
    }                                                 \
  } while (0)

int main() {
  int failures = 0;
  RUN(check_empty_tree_remeasures_zero);
  RUN(check_clean_unchanged_scale_remeasures_zero);
  RUN(check_dirty_nodes_remeasured_and_cleared);
  RUN(check_scale_change_remeasures_only_stale_nodes);
  RUN(check_second_pass_is_quiet);
  RUN(check_text_and_size_preserved_by_remeasure);
  RUN(check_empty_text_node_remeasured_without_crash);
  RUN(check_out_of_range_scale_stays_finite);
  RUN(check_signatures_match_plan);
  if (failures == 0) std::printf("scale_remeasure_test: all passing\n");
  return failures == 0 ? 0 : 1;
}
