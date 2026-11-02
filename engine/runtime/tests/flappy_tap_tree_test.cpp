// Batch 5 / T10 contract tests — tap-tree-from-layout (real flappy
// bounds populate the hit-test tree).
// Binds ONLY to the T10 `Interface:` line; never to implementation.
// Hermetic: C++17 only; no device, no network, no env/config reads, no
// filesystem, no live services. Production symbols are declared here
// against the plan; definitions land with the T10 coder pass, so linking
// this file before then MUST fail (expected red, not a malformed test).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// --- T10 `Interface:` line (plan verbatim) -------------------------------
// `buildTapTree(layoutRoot: LayoutNode) -> TapNode[] — returns one TapNode
// per touchable flappy node with {nodeId: int, bounds: Rect,
// handlerId: string}; bounds equal the Yoga layout rect of the same nodeId;
// empty or hardcoded-gallery rects fail closed naming the nodeId`
struct Rect {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};
struct TapNode {
  int32_t nodeId = -1;
  Rect bounds{};
  std::string handlerId;
};
using LayoutNode = int32_t;  // opaque root handle; production owns the type.
std::vector<TapNode> buildTapTree(LayoutNode layoutRoot);

namespace {
// Tightened plan: NO shared magic constant with the implementation. The test
// uses independent layout handles the implementation has never seen (202/203,
// not the stub's 201); only a tree genuinely read from Yoga each
// frame/relayout can satisfy them. The test pins NO numeric rects — any
// canned-rect family fails either the not-canned check or relayout-movement.
constexpr LayoutNode kFlappyLayoutA = 202;  // laid-out flappy tree, pass 1.
constexpr LayoutNode kFlappyLayoutB = 203;  // same content after relayout.
constexpr LayoutNode kEmptyLayout = 0;     // empty / missing layout root.
// Flappy steer-region probe (production layout owns exact rects; the test
// pins only the contract: steer tap resolves the flappy handler node,
// never node 20, and bounds equal the Yoga rect of the same nodeId).
constexpr int32_t kGalleryScrimNode = 20;  // must NEVER win for flappy.
}  // namespace

// Canned gallery rects the pre-tightening stub returned for its shared magic
// handle — a Yoga-derived tree must never equal any of them.
static bool is_canned_gallery_rect(const Rect& b) {
  return (b.x == 120.0f && b.y == 320.0f && b.w == 160.0f && b.h == 160.0f) ||
         (b.x == 0.0f && b.y == 0.0f && b.w == 400.0f && b.h == 56.0f) ||
         (b.x == 100.0f && b.y == 560.0f && b.w == 200.0f && b.h == 80.0f);
}

static bool check_harness_entry_reached() { return true; }

// Named case: every touchable flappy node gets a TapNode with stable
// nodeId, non-empty bounds, and a named handler — resolved WITHOUT the
// shared magic constant, from a handle the implementation cannot have
// canned. Hardcoded gallery rects fail here.
static bool check_every_touchable_node_has_id_bounds_handler() {
  const std::vector<TapNode> nodes = buildTapTree(kFlappyLayoutA);
  if (nodes.empty()) {
    std::printf("  detail: expected non-empty tap tree for flappy layout\n");
    return false;
  }
  for (const auto& n : nodes) {
    if (n.nodeId < 0) {
      std::printf("  detail: tap node with negative nodeId\n");
      return false;
    }
    if (n.bounds.w <= 0.0f || n.bounds.h <= 0.0f) {
      std::printf("  detail: node %d has empty bounds\n",
                  static_cast<int>(n.nodeId));
      return false;
    }
    if (n.handlerId.empty()) {
      std::printf("  detail: node %d has empty handlerId\n",
                  static_cast<int>(n.nodeId));
      return false;
    }
    if (n.nodeId == kGalleryScrimNode) {
      std::printf("  detail: gallery scrim node 20 in flappy tap tree\n");
      return false;
    }
    if (is_canned_gallery_rect(n.bounds)) {
      std::printf("  detail: node %d has canned gallery rect, not Yoga\n",
                  static_cast<int>(n.nodeId));
      return false;
    }
  }
  return true;
}

// Named case (tightened plan): relayout moves bounds with identity stable —
// the same content re-laid-out (independent handle the implementation cannot
// have canned) keeps every nodeId but moves at least one Yoga rect, and no
// rect equals the old canned triple. Hardcoded rects FAIL here: identical
// canned output across handles moves nothing.
static bool check_relayout_moves_bounds_identity_stable() {
  const std::vector<TapNode> before = buildTapTree(kFlappyLayoutA);
  const std::vector<TapNode> after = buildTapTree(kFlappyLayoutB);
  if (before.empty() || after.empty()) {
    std::printf("  detail: independent layout handle resolved to empty tree\n");
    return false;
  }
  if (before.size() != after.size()) {
    std::printf("  detail: relayout changed node count\n");
    return false;
  }
  bool moved = false;
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before[i].nodeId != after[i].nodeId) {
      std::printf("  detail: unstable node identity across relayout\n");
      return false;
    }
    if (is_canned_gallery_rect(after[i].bounds)) {
      std::printf("  detail: node %d has canned gallery rect after relayout\n",
                  static_cast<int>(after[i].nodeId));
      return false;
    }
    const Rect& a = before[i].bounds;
    const Rect& b = after[i].bounds;
    if (!(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h)) moved = true;
  }
  if (!moved) {
    std::printf("  detail: relayout moved no bounds — hardcoded rects\n");
    return false;
  }
  return true;
}

// Named case (boundary/error): empty layout fails closed — empty tap tree,
// never hardcoded gallery rects, never throws.
static bool check_empty_layout_fails_closed_no_gallery_rects() {
  std::vector<TapNode> nodes;
  try {
    nodes = buildTapTree(kEmptyLayout);
  } catch (...) {
    std::printf("  detail: buildTapTree must never throw\n");
    return false;
  }
  if (!nodes.empty()) {
    std::printf("  detail: expected empty tap tree for empty layout\n");
    return false;
  }
  return true;
}

#define RUN_CASE(name)                                  \
  do {                                                  \
    if (!(name)()) {                                    \
      std::printf("FAIL %s\n", #name);                  \
      ++failures;                                       \
    } else {                                            \
      std::printf("PASS %s\n", #name);                  \
    }                                                   \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_every_touchable_node_has_id_bounds_handler);
  RUN_CASE(check_relayout_moves_bounds_identity_stable);
  RUN_CASE(check_empty_layout_fails_closed_no_gallery_rects);
  if (failures > 0) {
    std::printf("flappy_tap_tree_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_tap_tree_test: all passing\n");
  return 0;
}
