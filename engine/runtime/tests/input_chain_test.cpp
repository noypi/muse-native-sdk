// Batch 2 / T4 contract tests — FR-INPUT-011 / AC-INPUT-020 (device tap
// chain, native hit-test + gesture-arena wiring half).
// Binds ONLY to the T4 `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed engine headers only; no device, no network, no
// env/config reads, no filesystem, no live services. HitTestTree and
// GestureArena are consume-only: their own units are NOT re-tested here,
// only the new wiring contract (ResolveTapTarget / ClaimTap) over real
// laid-out bounds (never the `touch_detail` 100x100 double rect).
#include <cstdint>
#include <cstdio>

#include "runtime/input_chain.h"

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

namespace {

constexpr int32_t kRootId = 10;
constexpr int32_t kButtonId = 21;
constexpr int32_t kChildId = 22;
constexpr int32_t kPointer0 = 1;

// Layout-engine bounds (the consume-only Interfaces row): a 400x800 root
// with a real laid-out Button-filled at (50,100)-(250,180).
HitTestTree make_tree_with_button() {
  HitTestTree tree;
  tree.AddNode(kRootId, 0, HitRect{0.0f, 0.0f, 400.0f, 800.0f});
  tree.AddNode(kButtonId, kRootId, HitRect{50.0f, 100.0f, 200.0f, 80.0f});
  return tree;
}

input::NormalizedEvent down_at(int32_t pointer, float x, float y) {
  input::AxisState axes{};
  axes.x = x;
  axes.y = y;
  axes.pressure = 1.0f;
  return input::NormalizeMotion(pointer, input::ToolType::Touch, 0u, axes, 0u,
                                input::SourceClass::Touch);
}

// Named case: a tap inside the real laid-out button bounds resolves its node.
bool check_resolve_inside_button_returns_button_node() {
  HitTestTree tree = make_tree_with_button();
  const int32_t got = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (got != kButtonId) {
    std::printf("  detail: expected %d, got %d\n", static_cast<int>(kButtonId),
                static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: a nested child wins over its parent (depth-wins, per the
// HitTestTree contract the chain consumes).
bool check_resolve_nested_child_wins_over_parent() {
  HitTestTree tree = make_tree_with_button();
  tree.AddNode(kChildId, kButtonId, HitRect{50.0f, 100.0f, 60.0f, 40.0f});
  const int32_t got = ResolveTapTarget(tree, 80.0f, 120.0f);
  if (got != kChildId) {
    std::printf("  detail: expected %d, got %d\n", static_cast<int>(kChildId),
                static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: a tap far outside every laid-out bound resolves -1 with no
// node to claim.
bool check_resolve_outside_returns_minus_one() {
  HitTestTree tree = make_tree_with_button();
  const int32_t got = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: an empty tree resolves -1 (no layout yet: fail closed, never a
// phantom node id).
bool check_resolve_empty_tree_returns_minus_one() {
  HitTestTree tree;
  const int32_t got = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: malformed negative coordinates resolve -1 (input validation:
// untrusted MotionEvent fields must not address a node).
bool check_resolve_negative_coords_returns_minus_one() {
  HitTestTree tree = make_tree_with_button();
  const int32_t got = ResolveTapTarget(tree, -5.0f, -5.0f);
  if (got != -1) {
    std::printf("  detail: expected -1, got %d\n", static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: a tap (down, no drag) claims its resolved target as winner.
bool check_claim_tap_after_down_returns_claimed_winner() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  arena.AddPointer(down_at(kPointer0, 150.0f, 140.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPointer0, kButtonId);
  if (!got.claimed || got.winnerId != kButtonId) {
    std::printf("  detail: expected claimed winner %d, got winner %d "
                "claimed=%d\n",
                static_cast<int>(kButtonId), static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case: a drag past slop (48px >> kTouchSlopPx 8px) scrolls and never
// fires the tap: the tap claim comes back unclaimed.
bool check_claim_tap_past_slop_drag_is_unclaimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  arena.AddPointer(down_at(kPointer0, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPointer0, 150.0f, 188.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPointer0, kButtonId);
  if (got.claimed) {
    std::printf("  detail: expected unclaimed after 48px drag, got winner "
                "%d claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case: sub-slop jitter (1px < kTouchSlopPx 8px) keeps the tap claimed
// on its target (ASSUMPTION: A2 slop source of truth; see contract).
bool check_claim_tap_sub_slop_jitter_stays_claimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  arena.AddPointer(down_at(kPointer0, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPointer0, 150.0f, 141.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPointer0, kButtonId);
  if (!got.claimed || got.winnerId != kButtonId) {
    std::printf("  detail: expected claimed winner %d after 1px jitter, got "
                "winner %d claimed=%d\n",
                static_cast<int>(kButtonId), static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case: a never-seen pointer claims nothing (fail closed: no phantom
// winner for an untracked MotionEvent pointer id).
bool check_claim_tap_unknown_pointer_is_unclaimed() {
  GestureArena arena;
  const ArenaAction got = ClaimTap(arena, 999, kButtonId);
  if (got.claimed) {
    std::printf("  detail: expected unclaimed for unknown pointer, got "
                "winner %d claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case: an outside tap (target -1) never produces a tap claim.
bool check_claim_tap_outside_target_is_unclaimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  arena.AddPointer(down_at(kPointer0, 10000.0f, 10000.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (target != -1) return false;
  const ArenaAction got = ClaimTap(arena, kPointer0, target);
  if (got.claimed) {
    std::printf("  detail: expected unclaimed for outside target, got winner "
                "%d claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_resolve_inside_button_returns_button_node);
  RUN_CASE(check_resolve_nested_child_wins_over_parent);
  RUN_CASE(check_resolve_outside_returns_minus_one);
  RUN_CASE(check_resolve_empty_tree_returns_minus_one);
  RUN_CASE(check_resolve_negative_coords_returns_minus_one);
  RUN_CASE(check_claim_tap_after_down_returns_claimed_winner);
  RUN_CASE(check_claim_tap_past_slop_drag_is_unclaimed);
  RUN_CASE(check_claim_tap_sub_slop_jitter_stays_claimed);
  RUN_CASE(check_claim_tap_unknown_pointer_is_unclaimed);
  RUN_CASE(check_claim_tap_outside_target_is_unclaimed);
  if (failures > 0) {
    std::printf("input_chain_test: %d failing\n", failures);
    return 1;
  }
  std::printf("input_chain_test: all passing\n");
  return 0;
}
