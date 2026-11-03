// Batch 5 / T10 contract tests — tap-tree-from-layout (real flappy
// bounds populate the hit-test tree).
// Binds ONLY to the T10 `Interface:` line; never to implementation.
// Hermetic: C++17 only; no device, no network, no env/config reads, no
// filesystem, no live services. Production symbols are declared here
// against the plan; definitions land with the T10 coder pass, so linking
// this file before then MUST fail (expected red, not a malformed test).
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

// Real device-lane hit-test type (consume-only, relative include per
// gallery_layering_test.cpp — no CMake change).
#include "../../input/include/hit_test.h"

// T6 seam setup header: buildTapTree resolves layout providers through the
// content-tag registry; explicit registration keeps the suite independent
// of static-init order.
#include "runtime/content_registry.h"

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

// JNI device-lane guard (input_chain.cpp, global namespace): deepest laid-out
// node at (x, y), or -1 outside every bound. Security-review pin: +Inf x/y
// must resolve -1 (fail closed, never a fire).
int32_t ResolveTapTarget(const HitTestTree& tree, float x, float y);

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
// Test-local layout providers (hermetic fake standing in for the removed
// content module): three touchables (31/32/33) with named handlers and
// provider-owned rects. The relayout root (203) keeps node identity but
// moves every rect, so only a tree genuinely read from the provider each
// frame/relayout can satisfy the cases below; the suite pins no numeric
// rects. Unregistered roots (0, others) expose nothing.
int test_tap_count(LayoutNode root) {
  return (root == kFlappyLayoutA || root == kFlappyLayoutB) ? 3 : 0;
}
std::int32_t test_tap_node_id(LayoutNode root, int index) {
  if (root != kFlappyLayoutA && root != kFlappyLayoutB) return -1;
  static const std::int32_t kIds[] = {31, 32, 33};
  return (index >= 0 && index < 3) ? kIds[index] : -1;
}
const char* test_tap_handler(LayoutNode root, int index) {
  if (root != kFlappyLayoutA && root != kFlappyLayoutB) return nullptr;
  static const char* const kHandlers[] = {"onTouchStart", "onHudTouch",
                                          "onPipeTouch"};
  return (index >= 0 && index < 3) ? kHandlers[index] : nullptr;
}
bool test_tap_bounds(LayoutNode root, std::int32_t nodeId, float* x, float* y,
                       float* w, float* h) {
  if (x == nullptr || y == nullptr || w == nullptr || h == nullptr) {
    return false;
  }
  // Provider-owned rects: distinct per node, shifted on relayout, never
  // the canned gallery triple (see is_canned_gallery_rect).
  const float dx = (root == kFlappyLayoutB) ? 17.0f : 0.0f;
  const float dy = (root == kFlappyLayoutB) ? -11.0f : 0.0f;
  if (root != kFlappyLayoutA && root != kFlappyLayoutB) return false;
  switch (nodeId) {
    case 31:
      *x = 140.0f + dx;
      *y = 390.0f + dy;
      *w = 44.0f;
      *h = 44.0f;
      return true;
    case 32:
      *x = 60.0f + dx;
      *y = 120.0f + dy;
      *w = 200.0f;
      *h = 60.0f;
      return true;
    case 33:
      *x = 250.0f + dx;
      *y = 500.0f + dy;
      *w = 90.0f;
      *h = 70.0f;
      return true;
    default:
      return false;
  }
}
bool RegisterTestTapProviders() {
  runtime::ContentHandlers handlers;
  handlers.tapCount = &test_tap_count;
  handlers.tapNodeId = &test_tap_node_id;
  handlers.tapHandler = &test_tap_handler;
  handlers.tapBounds = &test_tap_bounds;
  // First wins: re-registration across runs keeps the first bundle.
  (void)GlobalContentRegistry().Register("test-tap-tree", handlers);
  return GlobalContentRegistry().Resolve("test-tap-tree") != nullptr;
}
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

// Named case (security — JNI-lane +Inf admission, Batch 5 review): +Inf
// coordinates on the device lane fail closed to -1, never a fire. Green
// today via finite-geometry miss (HitTest matches no finite rect); pins the
// observable contract so the coder's isfinite-guard follow-up cannot regress
// it, and any future Inf-matching geometry fails loudly here.
static bool check_jni_lane_plus_inf_fails_closed() {
  const float kInf = std::numeric_limits<float>::infinity();
  const float kNaN = std::numeric_limits<float>::quiet_NaN();
  try {
    HitTestTree tree;
    tree.AddNode(10, 0, HitRect{0.0f, 0.0f, 400.0f, 800.0f});
    tree.AddNode(21, 10, HitRect{50.0f, 100.0f, 200.0f, 80.0f});
    // Sanity: the tree is real — a finite steer-region tap resolves.
    if (ResolveTapTarget(tree, 150.0f, 140.0f) != 21) {
      std::printf("  detail: finite probe did not resolve node 21\n");
      return false;
    }
    if (ResolveTapTarget(tree, kInf, kInf) != -1) {
      std::printf("  detail: (+Inf,+Inf) admitted to a tap target\n");
      return false;
    }
    if (ResolveTapTarget(tree, kInf, 140.0f) != -1 ||
        ResolveTapTarget(tree, 150.0f, kInf) != -1) {
      std::printf("  detail: single-lane +Inf admitted to a tap target\n");
      return false;
    }
    if (ResolveTapTarget(tree, kNaN, 140.0f) != -1) {
      std::printf("  detail: NaN admitted to a tap target\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: ResolveTapTarget must never throw\n");
    return false;
  }
  return true;
}

// ---- T2 (bug-runtime-009-010 Option A) precondition ----
// T4 dispatches taps on any evaluated/hit-test node with a registered
// handler (never only 31). This case pins the surface T4 builds on: the
// tap tree exposes non-31 touchables (32/33) with Yoga-derived bounds and
// named handlers. Expected GREEN today and after T4; the RED dispatch
// half lives in flappy_tap_js_dispatch_test.cpp.
static bool check_tap_tree_exposes_non31_touchables_with_handlers() {
  const std::vector<TapNode> nodes = buildTapTree(kFlappyLayoutA);
  bool saw32 = false;
  bool saw33 = false;
  for (const auto& n : nodes) {
    if (n.nodeId == 32 || n.nodeId == 33) {
      if (n.handlerId.empty()) {
        std::printf("  detail: node %d has empty handlerId\n",
                    static_cast<int>(n.nodeId));
        return false;
      }
      if (n.bounds.w <= 0.0f || n.bounds.h <= 0.0f) {
        std::printf("  detail: node %d has empty bounds\n",
                    static_cast<int>(n.nodeId));
        return false;
      }
      if (is_canned_gallery_rect(n.bounds)) {
        std::printf("  detail: node %d has canned gallery rect\n",
                    static_cast<int>(n.nodeId));
        return false;
      }
      if (n.nodeId == 32) saw32 = true;
      if (n.nodeId == 33) saw33 = true;
    }
  }
  if (!saw32 || !saw33) {
    std::printf("  detail: tap tree exposes no non-31 touchables\n");
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
  // T6 seam setup (FR-RUNTIME-013): layout providers resolve through the
  // content-tag registry via explicit test-local registration. Idempotent.
  if (!RegisterTestTapProviders()) {
    std::printf("FAIL test_tap_provider_registration\n");
    return 1;
  }
  std::printf("PASS test_tap_provider_registration\n");
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_every_touchable_node_has_id_bounds_handler);
  RUN_CASE(check_relayout_moves_bounds_identity_stable);
  RUN_CASE(check_empty_layout_fails_closed_no_gallery_rects);
  RUN_CASE(check_jni_lane_plus_inf_fails_closed);
  RUN_CASE(check_tap_tree_exposes_non31_touchables_with_handlers);
  if (failures > 0) {
    std::printf("flappy_tap_tree_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_tap_tree_test: all passing\n");
  return 0;
}
