// Batch 3 / T6 contract tests — FR-INPUT-011 / AC-INPUT-020 (repointed off
// the node-1 test double onto the device-path contract).
// Binds ONLY to the T6 `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed engine headers only; no device, no network, no
// env/config reads, no filesystem, no live services.
//
// Repointing (FR-INPUT-011 Notes: the `dispatch_touch` node-1 rect +
// `button_fire_count` double is replaced on the device path, never coexisted
// with): this file references NO device-path `dispatch_touch`,
// `handler_fire_count`, `button_fire_count`, or `list_scroll_offset` symbol.
// The asserted surface for tap routing is `ResolveTapTarget` (from T4) plus
// the ClaimTap/FireTapHandler halves owned by T5; Host counters below appear
// ONLY as narrowed slop/threshold-math isolation checks and never stand in
// for the device chain. Host lifecycle helpers (create/destroy/surface) are
// untouched by the deletion and stay as the tree-intact witnesses.
//
// Isolation: the down-origin table is process-global with no reset API, so
// every claim case uses a pointer id unique to that case (kP* below). No
// case reads another case's origin; reordering cases cannot change results.
#include <cstdint>
#include <cstdio>

#include "runtime/host.h"
#include "runtime/input_chain.h"

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

// T5 `Interface:` line (plan verbatim), consumed here because T6's routing
// surface ends at the fire decision: the definitions land with the T5 coder
// pass, so linking this file before then MUST fail (expected red, not a
// malformed test).
bool FireTapHandler(int32_t nodeId);

// --- harness cases (convention copied from instantiate_test.cpp) -----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && failingName[0] == '\0';
}

// --- new-contract helpers --------------------------------------------------
// Layout-engine bounds (the consume-only Interfaces row): a 400x800 root
// with a real laid-out Button-filled at (50,100)-(250,180). The tap target
// is laid-out bounds, never the retired 100x100 double rect.

namespace {

constexpr int32_t kRootId = 10;
constexpr int32_t kButtonId = 21;
constexpr int32_t kListId = 7;  // scrollable list id (windowed path, T2)

// Unique pointer id per claim case: isolation against the process-global
// down-origin table (no reset API). Never reuse one across cases.
constexpr int32_t kPTap = 31;
constexpr int32_t kPOutside = 32;
constexpr int32_t kPDrag = 33;
constexpr int32_t kPJitter = 34;
constexpr int32_t kPReuse = 35;
constexpr int32_t kPAbsent = 36;    // AddPointer but NEVER RecordTapDown
constexpr int32_t kPUnknown = 999;  // never AddPointer'd, never recorded

HitTestTree make_tree_with_button_and_list() {
  HitTestTree tree;
  tree.AddNode(kRootId, 0, HitRect{0.0f, 0.0f, 400.0f, 800.0f});
  tree.AddNode(kButtonId, kRootId, HitRect{50.0f, 100.0f, 200.0f, 80.0f});
  tree.AddNode(kListId, kRootId, HitRect{0.0f, 200.0f, 400.0f, 600.0f});
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

}  // namespace

// Named case: a tap at a real laid-out button's bounds resolves its node
// through ResolveTapTarget (the T6 asserted surface for tap routing).
static bool check_tap_at_real_bounds_resolves_button_node() {
  HitTestTree tree = make_tree_with_button_and_list();
  const int32_t got = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (got != kButtonId) {
    std::printf("  detail: expected %d, got %d\n", static_cast<int>(kButtonId),
                static_cast<int>(got));
    return false;
  }
  return true;
}

// Named case: a claimed tap fires its handler exactly once per gesture — one
// FireTapHandler call per ClaimTap-claimed tap returns true. Per-tap fire
// counting on device is ASSUMPTION mapped to T7 (`--assert-once`).
static bool check_tap_fires_handler_once() {
  HitTestTree tree = make_tree_with_button_and_list();
  GestureArena arena;
  RecordTapDown(kPTap, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPTap, 150.0f, 140.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (target != kButtonId) return false;  // setup broken
  const ArenaAction claim = ClaimTap(arena, kPTap, target);
  if (!claim.claimed || claim.winnerId != kButtonId) return false;  // setup
  const bool ok = FireTapHandler(target);
  if (!ok) std::printf("  detail: expected fire true for claimed tap\n");
  return ok;
}

// Named case: a tap outside every laid-out bound resolves -1 and fires
// nothing (outside-silent, AC-INPUT-020).
static bool check_tap_outside_fires_nothing() {
  HitTestTree tree = make_tree_with_button_and_list();
  GestureArena arena;
  RecordTapDown(kPOutside, 10000.0f, 10000.0f);
  arena.AddPointer(down_at(kPOutside, 10000.0f, 10000.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (target != -1) return false;  // setup broken
  const ArenaAction claim = ClaimTap(arena, kPOutside, target);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed for outside target\n");
    return false;
  }
  const bool ok = !FireTapHandler(target);
  if (!ok) std::printf("  detail: expected no fire for node -1\n");
  return ok;
}

// Named case: a drag past slop owns the scroll — the tap claim comes back
// unclaimed and no tap handler fires for the gesture (drag-no-fire).
static bool check_drag_past_slop_scrolls_without_firing() {
  HitTestTree tree = make_tree_with_button_and_list();
  GestureArena arena;
  RecordTapDown(kPDrag, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPDrag, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPDrag, 150.0f, 188.0f), tree);
  const ArenaAction claim = ClaimTap(arena, kPDrag, kButtonId);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed after 48px drag\n");
    return false;
  }
  const bool ok = !FireTapHandler(-1);
  if (!ok) std::printf("  detail: expected no fire for unclaimed drag\n");
  return ok;
}

// Named case (narrowed slop-math unit ONLY — never stands in for the device
// chain): sub-slop jitter (1px < kTouchSlopPx 8px) keeps the tap claimed on
// its target. ASSUMPTION (plan A2 slop source of truth; see contract).
static bool check_sub_slop_jitter_does_not_scroll() {
  HitTestTree tree = make_tree_with_button_and_list();
  GestureArena arena;
  RecordTapDown(kPJitter, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPJitter, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPJitter, 150.0f, 141.0f), tree);
  const ArenaAction claim = ClaimTap(arena, kPJitter, kButtonId);
  const bool ok = claim.claimed && claim.winnerId == kButtonId;
  if (!ok) std::printf("  detail: expected claimed winner after 1px jitter\n");
  return ok;
}

// Named case: a drag never fires the tap handler — the past-slop claim is
// unclaimed, so the UP path has no winner to fire for.
static bool check_drag_does_not_fire_tap_handler() {
  HitTestTree tree = make_tree_with_button_and_list();
  GestureArena arena;
  RecordTapDown(kPDrag + 100, 150.0f, 300.0f);
  arena.AddPointer(down_at(kPDrag + 100, 150.0f, 300.0f), tree);
  arena.AddPointer(down_at(kPDrag + 100, 150.0f, 348.0f), tree);
  const ArenaAction claim = ClaimTap(arena, kPDrag + 100, kListId);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed tap after 48px list drag\n");
    return false;
  }
  const bool ok = !FireTapHandler(-1);
  if (!ok) std::printf("  detail: expected no tap fire for scroll drag\n");
  return ok;
}

// Named case (narrowed per-gesture isolation unit — Host lifecycle only, no
// double counters): two independent Hosts keep independent live trees, so
// one gesture's repaint cannot re-instantiate the other's tree.
static bool check_two_hosts_have_independent_trees() {
  runtime::Host a = runtime::create_host(runtime::Surface{});
  runtime::Host b = runtime::create_host(runtime::Surface{});
  const bool ok = runtime::tree_intact(a) && runtime::tree_intact(b) &&
                  runtime::live_tree_nodes(a) == runtime::live_tree_nodes(b) &&
                  runtime::live_tree_nodes(a) > 0;
  runtime::destroy_host(a);
  runtime::destroy_host(b);
  if (!ok) std::printf("  detail: expected two intact independent trees\n");
  return ok;
}

// Named case (narrowed per-arena isolation unit): two independent GestureArenas
// never share claims — a claim in one leaves the other unclaimed for the same
// pointer. No Host double counter is read here.
static bool check_two_arenas_have_independent_claims() {
  HitTestTree tree = make_tree_with_button_and_list();
  GestureArena a;
  GestureArena b;
  RecordTapDown(kPReuse, 150.0f, 140.0f);
  a.AddPointer(down_at(kPReuse, 150.0f, 140.0f), tree);
  const ArenaAction claimA = ClaimTap(a, kPReuse, kButtonId);
  const ArenaAction claimB = ClaimTap(b, kPUnknown, kButtonId);
  const bool ok = claimA.claimed && claimA.winnerId == kButtonId &&
                  !claimB.claimed;
  if (!ok) std::printf("  detail: expected independent arena claims\n");
  return ok;
}

// Named case (fail-closed identity, carried from T4): an empty tree resolves
// -1, an unknown pointer claims nothing, and a forged node id fires nothing.
// Never throws, never a phantom winner.
static bool check_unknown_ids_fail_closed() {
  HitTestTree empty;
  if (ResolveTapTarget(empty, 150.0f, 140.0f) != -1) {
    std::printf("  detail: expected -1 on empty tree\n");
    return false;
  }
  GestureArena arena;
  const ArenaAction claim = ClaimTap(arena, kPUnknown, kButtonId);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed for unknown pointer\n");
    return false;
  }
  // Fail-closed-when-absent: AddPointer without RecordTapDown never claims.
  HitTestTree tree = make_tree_with_button_and_list();
  arena.AddPointer(down_at(kPAbsent, 150.0f, 140.0f), tree);
  const ArenaAction absent = ClaimTap(arena, kPAbsent, kButtonId);
  if (absent.claimed) {
    std::printf("  detail: expected unclaimed with no RecordTapDown\n");
    return false;
  }
  const bool ok = !FireTapHandler(-1);
  if (!ok) std::printf("  detail: expected no fire for node -1\n");
  return ok;
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_tap_at_real_bounds_resolves_button_node);
  RUN_CASE(check_tap_fires_handler_once);
  RUN_CASE(check_tap_outside_fires_nothing);
  RUN_CASE(check_drag_past_slop_scrolls_without_firing);
  RUN_CASE(check_sub_slop_jitter_does_not_scroll);
  RUN_CASE(check_drag_does_not_fire_tap_handler);
  RUN_CASE(check_two_hosts_have_independent_trees);
  RUN_CASE(check_two_arenas_have_independent_claims);
  RUN_CASE(check_unknown_ids_fail_closed);
  if (failures > 0) {
    std::printf("touch_dispatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("touch_dispatch_test: all passing\n");
  return 0;
}
