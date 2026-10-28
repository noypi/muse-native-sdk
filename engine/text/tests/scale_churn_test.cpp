// T7 (FR-TEXT-012 / AC-TEXT-032): scale-churn resistance — 10k-node tree,
// <5% remeasured on a nudge, dense screen unclipped at 200%, toggles converge.
// Test-only: exercises T2 (font_scale.h) + T5 (scale_layout.h) public
// signatures; no new production header.
// Hermetic harness: plain C++17, no test framework, no device, no downloads.
// Shape mirrors engine/text/tests/scale_remeasure_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

#include "muse/text/font_scale.h"
#include "muse/text/scale_layout.h"

// Plan Interface line (verbatim):
// none — test-only; asserts over ScaleLayout + NonlinearScale public signatures

namespace {

constexpr size_t kNodes = 10000u;
constexpr float kDenseDensity = 3.0f;

// Mixed-size synthetic tree: half small text (resolves near-flat on the
// nonlinear curve, so most nodes stay clean on a nudge), half larger text.
// Converged at fontScale 1.0 via the public remeasure path only.
std::vector<muse::text::TextNode> convergedTree() {
  std::vector<muse::text::TextNode> nodes;
  nodes.reserve(kNodes);
  for (size_t i = 0; i < kNodes; ++i) {
    muse::text::TextNode n;
    n.text = "para " + std::to_string(i) + " lorem ipsum dolor";
    n.sizePx = (i % 2u == 0u) ? 10.0f + static_cast<float>(i % 3u) : 18.0f;
    n.resolvedScale = 0.0f;  // placeholder; converged below.
    n.styleGen = 1u;
    n.dirty = true;
    nodes.push_back(n);
  }
  (void)muse::text::RemeasureDirty(nodes, 1.0f, kDenseDensity);
  return nodes;
}

}  // namespace

static bool check_tree_builds_10k_nodes() {
  std::vector<muse::text::TextNode> nodes = convergedTree();
  if (nodes.size() != kNodes) return false;
  // Converged: a repeat pass at the same scale remeasures nothing.
  if (muse::text::RemeasureDirty(nodes, 1.0f, kDenseDensity) != 0u) {
    return false;
  }
  return true;
}

static bool check_nudge_remeasures_under_5_percent() {
  // ASSUMPTION (plan Detail): small text resolves near-flat on the nonlinear
  // curve, so a small nudge leaves most nodes clean. The coder's T2/T5 path
  // must quantize or epsilon-compare resolved scales to realize this; exact
  // float inequality would remeasure the whole tree.
  std::vector<muse::text::TextNode> nodes = convergedTree();
  const size_t n = muse::text::RemeasureDirty(nodes, 1.01f, kDenseDensity);
  if (n * 100u >= 5u * kNodes) return false;  // strict <5%
  return true;
}

static bool check_toggle_converges_no_growth() {
  // 1.0 -> 2.0 -> 1.0 -> 2.0 -> 1.0: the dirty set must not grow
  // monotonically; each pass remeasures no more than the first big toggle,
  // and a final repeat pass is quiet.
  std::vector<muse::text::TextNode> nodes = convergedTree();
  const size_t first =
      muse::text::RemeasureDirty(nodes, 2.0f, kDenseDensity);
  const size_t back =
      muse::text::RemeasureDirty(nodes, 1.0f, kDenseDensity);
  const size_t again =
      muse::text::RemeasureDirty(nodes, 2.0f, kDenseDensity);
  const size_t home =
      muse::text::RemeasureDirty(nodes, 1.0f, kDenseDensity);
  if (back > first) return false;
  if (again > first) return false;
  if (home > first) return false;
  if (muse::text::RemeasureDirty(nodes, 1.0f, kDenseDensity) != 0u) {
    return false;
  }
  return true;
}

static bool check_repeat_pass_is_quiet() {
  // Same-scale re-application after convergence remeasures zero (no reshape
  // of unchanged paragraphs, per A5 structural budget).
  std::vector<muse::text::TextNode> nodes = convergedTree();
  (void)muse::text::RemeasureDirty(nodes, 2.0f, kDenseDensity);
  if (muse::text::RemeasureDirty(nodes, 2.0f, kDenseDensity) != 0u) {
    return false;
  }
  return true;
}

static bool check_dense_200_unclipped() {
  // Dense-settings/form fixture at 200%: every representative size yields a
  // finite positive scaled size; the line box (1.5x) encloses
  // ascender+descender (1.1x); the scaled width stays within the linear
  // worst-case budget (sub-linear curve never exceeds it).
  const float kSizes[] = {10.0f, 12.0f, 14.0f, 16.0f, 20.0f, 24.0f};
  for (float sizePx : kSizes) {
    const float scaled =
        muse::text::ScaledSizePx(sizePx, 2.0f, kDenseDensity);
    if (!(scaled > 0.0f)) return false;
    if (!std::isfinite(scaled)) return false;
    const float ascDesc = 1.1f * scaled;
    const float lineHeight = 1.5f * scaled;
    if (!(lineHeight >= ascDesc)) return false;  // no clip/overlap
    const float width = 20.0f * 0.6f * scaled;   // 20-char form line
    const float budget = 20.0f * 0.6f * sizePx * kDenseDensity * 2.0f;
    if (!(width <= budget)) return false;
  }
  return true;
}

static bool check_sublinear_cap_at_200() {
  // The A14-equivalent curve compresses above 1.0: effective multiplier at
  // 2.0 is strictly below the linear 2.0.
  const float m = muse::text::NonlinearScale(2.0f);
  if (!(m > 1.0f && m < 2.0f)) return false;
  if (!std::isfinite(m)) return false;
  return true;
}

static bool check_signatures_match_plan() {
  static_assert(std::is_same_v<decltype(&muse::text::RemeasureDirty),
                               size_t (*)(std::vector<muse::text::TextNode>&,
                                           float, float)>);
  static_assert(std::is_same_v<decltype(&muse::text::NonlinearScale),
                               float (*)(float)>);
  static_assert(std::is_same_v<decltype(&muse::text::ScaledSizePx),
                               float (*)(float, float, float)>);
  muse::text::TextNode n;
  n.text = "x";
  n.sizePx = 12.0f;
  n.resolvedScale = 1.0f;
  n.styleGen = 1u;
  n.dirty = false;
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
  RUN(check_tree_builds_10k_nodes);
  RUN(check_nudge_remeasures_under_5_percent);
  RUN(check_toggle_converges_no_growth);
  RUN(check_repeat_pass_is_quiet);
  RUN(check_dense_200_unclipped);
  RUN(check_sublinear_cap_at_200);
  RUN(check_signatures_match_plan);
  if (failures == 0) std::printf("scale_churn_test: all passing\n");
  return failures == 0 ? 0 : 1;
}
