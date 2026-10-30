// Batch 3 / T5 contract tests — FR-INPUT-011 / AC-INPUT-020 + AC-INPUT-021
// (device tap chain: component-handler dispatch + write-through one-frame
// repaint half, with the page-flip double-handling disposition).
// Binds ONLY to the T5 `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed engine headers only; no device, no network, no
// env/config reads, no filesystem, no live services. HitTestTree and
// GestureArena are consume-only: their own units are NOT re-tested here,
// only the production tap order RecordTapDown (ACTION_DOWN) -> AddPointer ->
// ResolveTapTarget + ClaimTap (ACTION_UP) -> FireTapHandler, and the
// RepaintWithinOneFrame post-write-through hook.
//
// ClaimTap-exclusive routing (cleared-review binding b): every tap decision
// in these cases travels through ClaimTap; no direct arena read bypasses it.
// A FireTapHandler for a node that ClaimTap never claimed MUST return false.
// The device half (valid-env dispatch body, Java onTouchEvent forwarding,
// one-frame vsync proof, MainActivity disposition) is ASSUMPTION mapped to
// T7 device proof (see contract T5.md).
//
// Isolation: the down-origin table is process-global with no reset API, so
// every claim case uses a pointer id unique to that case (kP* below). No
// case reads another case's origin; reordering cases cannot change results.
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <limits>

#include "runtime/host.h"
#include "runtime/input_chain.h"

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

// T5 `Interface:` lines (plan verbatim). Declared here against the plan —
// the definitions land with the T5 coder pass, so linking this file before
// then MUST fail (expected red, not a malformed test).
bool FireTapHandler(int32_t nodeId);
bool RepaintWithinOneFrame(int32_t nodeId);

// PROVISIONAL production population path (review BLOCKER: tapTree never
// populated from layout bounds, so device dispatch resolves -1 forever).
// The coder owns the real name/signature (e.g. PopulateTapTreeFromLayout);
// this declaration is an ASSUMPTION for the hermetic cases below. If the
// coder names it differently, update these call sites (reconciliation, not
// weakening) and re-publish the contract. Until the coder defines it,
// linking this file MUST fail (expected red).
void PopulateTapTreeFromLayout(HitTestTree& tree);

namespace {

constexpr int32_t kRootId = 10;
constexpr int32_t kButtonId = 21;

// Unique pointer id per claim case: isolation against the process-global
// down-origin table (no reset API). Never reuse one across cases.
constexpr int32_t kPFire = 21;
constexpr int32_t kPDrag = 23;
constexpr int32_t kPDownUp = 24;
// Fresh pointer ids for the re-audit cases below (never reused elsewhere in
// this file; the origin table has no reset).
constexpr int32_t kPPopulated = 40;
constexpr int32_t kPIsolateA = 41;
constexpr int32_t kPIsolateB = 42;
constexpr int32_t kPClaimNoFire = 43;
constexpr int32_t kPNaN = 44;
constexpr int32_t kPReplay = 45;

// Auxiliary laid-out node for the claimed-but-never-fired repaint case:
// a real second button at (300,100)-(380,180) inside the 400x800 root.
constexpr int32_t kAuxId = 77;

// Same layout-engine bounds shape the T4 contract seeds (a 400x800 root
// with a real laid-out Button-filled at (50,100)-(250,180)): the tap target
// is laid-out bounds, never the `touch_detail` 100x100 double rect.
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

// Named case: a negative node id fires nothing (T5 interface: false when
// nodeId < 0). Forged/stale/zero ids fail closed the same way (binding a).
bool check_fire_negative_node_returns_false() {
  if (FireTapHandler(-1)) {
    std::printf("  detail: expected false for node -1\n");
    return false;
  }
  if (FireTapHandler(-999)) {
    std::printf("  detail: expected false for node -999\n");
    return false;
  }
  if (FireTapHandler(std::numeric_limits<int32_t>::min())) {
    std::printf("  detail: expected false for INT32_MIN\n");
    return false;
  }
  return true;
}

// Named case (security binding a): a forged node id that no layout ever
// produced fires nothing and does not crash (no deref, no handler lookup
// hit). Zero is not a laid-out node here either, so it stays silent.
bool check_fire_forged_and_zero_ids_fail_closed() {
  if (FireTapHandler(std::numeric_limits<int32_t>::max())) {
    std::printf("  detail: expected false for forged INT32_MAX\n");
    return false;
  }
  if (FireTapHandler(0)) {
    std::printf("  detail: expected false for node 0\n");
    return false;
  }
  return true;
}

// Named case (binding b, ClaimTap-exclusive routing): every tap decision in
// this file travels the production order RecordTapDown -> AddPointer ->
// ResolveTapTarget + ClaimTap -> FireTapHandler; no case reads the arena
// directly to arm a fire. A fire is therefore always preceded by its
// gesture's own ClaimTap returning claimed with the fired node as winner.
bool check_claimed_tap_fires() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPFire, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPFire, 150.0f, 140.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (target != kButtonId) {
    std::printf("  detail: expected target %d, got %d\n",
                static_cast<int>(kButtonId), static_cast<int>(target));
    return false;
  }
  const ArenaAction claim = ClaimTap(arena, kPFire, target);
  if (!claim.claimed || claim.winnerId != kButtonId) {
    std::printf("  detail: expected claimed winner %d\n",
                static_cast<int>(kButtonId));
    return false;
  }
  if (!FireTapHandler(target)) {
    std::printf("  detail: expected fire true for claimed tap\n");
    return false;
  }
  return true;
}

// Named case (AC-INPUT-020 exactly-once, hermetic half): two sequential
// claimed taps each fire (repeatability — no one-shot global latch may break
// the second tap), and in-test each claimed tap maps to exactly one
// FireTapHandler call returning true. Per-tap fire counting on device
// (logcat `--assert-once`: one handler invocation per tap, no double-fire)
// is ASSUMPTION mapped to T7.
bool check_second_claimed_tap_fires_again() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPDownUp, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPDownUp, 150.0f, 140.0f), tree);
  const ArenaAction claim = ClaimTap(arena, kPDownUp, kButtonId);
  if (!claim.claimed) {
    std::printf("  detail: expected claimed tap for second gesture\n");
    return false;
  }
  if (!FireTapHandler(kButtonId)) {
    std::printf("  detail: expected fire true for second claimed tap\n");
    return false;
  }
  return true;
}

// Named case (binding c, production DOWN/UP path): ACTION_DOWN records the
// down origin and adds the pointer; ACTION_UP resolves the tap target from
// laid-out bounds and claims before firing. A tap outside resolves -1 and
// fires nothing.
bool check_down_up_path_outside_fires_nothing() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPFire, 10000.0f, 10000.0f);
  arena.AddPointer(down_at(kPFire, 10000.0f, 10000.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (target != -1) {
    std::printf("  detail: expected -1 outside, got %d\n",
                static_cast<int>(target));
    return false;
  }
  const ArenaAction claim = ClaimTap(arena, kPFire, target);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed for outside target\n");
    return false;
  }
  if (FireTapHandler(target)) {
    std::printf("  detail: expected false firing node -1\n");
    return false;
  }
  return true;
}

// Named case (AC-INPUT-020 drag-no-fire): a drag past slop owns the scroll,
// so the tap claim comes back unclaimed and no tap handler fires for it.
bool check_past_slop_drag_claims_nothing_and_fires_nothing() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPDrag, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPDrag, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPDrag, 150.0f, 188.0f), tree);
  const ArenaAction claim = ClaimTap(arena, kPDrag, kButtonId);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed after 48px drag\n");
    return false;
  }
  if (FireTapHandler(-1)) {
    std::printf("  detail: expected no fire for unclaimed drag\n");
    return false;
  }
  return true;
}

// Named case (AC-INPUT-021 one-frame hook): a fired button's write-through
// repaint posts within one vsync. Hermetically only the hook's contract is
// asserted (true for the fired node, false for outside/forged ids); the
// vsync timing itself is device-only (ASSUMPTION -> T7).
bool check_repaint_fired_button_returns_true() {
  if (!RepaintWithinOneFrame(kButtonId)) {
    std::printf("  detail: expected true repaint for fired node %d\n",
                static_cast<int>(kButtonId));
    return false;
  }
  return true;
}

// Named case: no repaint is posted for a tap that fired nothing (outside,
// forged). A silent tap must not schedule work.
bool check_repaint_outside_and_forged_returns_false() {
  if (RepaintWithinOneFrame(-1)) {
    std::printf("  detail: expected false repaint for node -1\n");
    return false;
  }
  if (RepaintWithinOneFrame(std::numeric_limits<int32_t>::max())) {
    std::printf("  detail: expected false repaint for forged id\n");
    return false;
  }
  return true;
}

// Named case (AC-INPUT-021 no re-instantiation, hermetic half): posting the
// write-through repaint leaves the live tree intact — the host still holds
// its nodes after the repaint call. Full proof (no new tree object on
// device, frame posted on the 009 vsync path) is ASSUMPTION -> T7.
bool check_repaint_leaves_live_tree_intact() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  const std::size_t before = runtime::live_tree_nodes(h);
  const bool posted = RepaintWithinOneFrame(kButtonId);
  const bool intact =
      runtime::surface_alive(h) && runtime::tree_intact(h) &&
      runtime::live_tree_nodes(h) == before;
  runtime::destroy_host(h);
  if (!posted) {
    std::printf("  detail: expected repaint posted for node %d\n",
                static_cast<int>(kButtonId));
    return false;
  }
  if (!intact) {
    std::printf("  detail: expected live tree intact after repaint\n");
    return false;
  }
  return true;
}

// Named case (review BLOCKER, hermetic half): a tree populated via the
// production population path resolves a real laid-out button node at the
// button's bounds (not -1), and the resolved target fires after its claim.
// ASSUMPTION: the layout bounds behind the population path match the
// T4-seeded shape (400x800 root, Button-filled at (50,100)-(250,180)), so
// (150,140) is inside a real button. An unpopulated tree resolves -1
// (fail-closed, current behavior pinned).
bool check_populated_tree_target_fires() {
  HitTestTree populated;
  PopulateTapTreeFromLayout(populated);
  const int32_t target = ResolveTapTarget(populated, 150.0f, 140.0f);
  if (target == -1) {
    std::printf("  detail: populated tree resolved -1 at button bounds\n");
    return false;
  }
  GestureArena arena;
  RecordTapDown(kPPopulated, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPPopulated, 150.0f, 140.0f), populated);
  const ArenaAction claim = ClaimTap(arena, kPPopulated, target);
  if (!claim.claimed) {
    std::printf("  detail: expected claimed tap on populated-tree target\n");
    return false;
  }
  if (!FireTapHandler(target)) {
    std::printf("  detail: expected fire true for populated-tree tap\n");
    return false;
  }
  HitTestTree unpopulated;
  if (ResolveTapTarget(unpopulated, 150.0f, 140.0f) != -1) {
    std::printf("  detail: expected -1 on unpopulated tree\n");
    return false;
  }
  return true;
}

// Named case (review MAJOR, per-arena isolation): a claim on arena/handle A
// must NOT let a different arena/handle B fire the same node id. Chain B
// goes DOWN and adds its pointer but never claims, so firing the node from
// B's perspective must be false. NOTE: the current FireTapHandler(nodeId)
// signature carries no arena, so this case can only pin the end behavior
// (false); the coder's per-arena ledger fix may need a scoped fire entry
// point — that is a coder design decision + contract reconciliation, not a
// test weakening.
bool check_claim_on_arena_a_does_not_let_arena_b_fire() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arenaA;
  GestureArena arenaB;
  RecordTapDown(kPIsolateA, 150.0f, 140.0f);
  arenaA.AddPointer(down_at(kPIsolateA, 150.0f, 140.0f), tree);
  const ArenaAction claimA = ClaimTap(arenaA, kPIsolateA, kButtonId);
  if (!claimA.claimed) {
    std::printf("  detail: setup broken, expected claim on arena A\n");
    return false;
  }
  RecordTapDown(kPIsolateB, 150.0f, 140.0f);
  arenaB.AddPointer(down_at(kPIsolateB, 150.0f, 140.0f), tree);
  // B never claimed: firing the node id from B's chain must be false.
  if (FireTapHandler(kButtonId)) {
    std::printf("  detail: phantom fire on unfired chain B\n");
    return false;
  }
  return true;
}

// Named case (review MAJOR, repaint predicate): a claimed-but-never-fired
// node posts no repaint. Only a fired node reads as posted.
bool check_claimed_but_never_fired_posts_no_repaint() {
  HitTestTree tree = make_tree_with_button();
  tree.AddNode(kAuxId, kRootId, HitRect{300.0f, 100.0f, 80.0f, 80.0f});
  GestureArena arena;
  RecordTapDown(kPClaimNoFire, 340.0f, 140.0f);
  arena.AddPointer(down_at(kPClaimNoFire, 340.0f, 140.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 340.0f, 140.0f);
  if (target != kAuxId) {
    std::printf("  detail: setup broken, expected target %d\n",
                static_cast<int>(kAuxId));
    return false;
  }
  const ArenaAction claim = ClaimTap(arena, kPClaimNoFire, target);
  if (!claim.claimed) {
    std::printf("  detail: setup broken, expected claimed tap\n");
    return false;
  }
  // Never fired: the repaint hook must read false.
  if (RepaintWithinOneFrame(kAuxId)) {
    std::printf("  detail: expected false repaint for never-fired node\n");
    return false;
  }
  return true;
}

// Named case (security: untrusted MotionEvent fields): a NaN DOWN origin
// fails closed — the in-bounds UP never claims and never fires.
bool check_nan_down_origin_fails_closed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  RecordTapDown(kPNaN, nan, 140.0f);
  arena.AddPointer(down_at(kPNaN, 150.0f, 140.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (target != kButtonId) {
    std::printf("  detail: setup broken, expected in-bounds target\n");
    return false;
  }
  const ArenaAction claim = ClaimTap(arena, kPNaN, target);
  if (claim.claimed) {
    std::printf("  detail: expected unclaimed tap for NaN down origin\n");
    return false;
  }
  if (FireTapHandler(target)) {
    std::printf("  detail: expected no fire for NaN-origin gesture\n");
    return false;
  }
  return true;
}

// Named case (UP replay contract): each UP that re-claims fires again
// (repeat fire per UP — the documented repeatability behavior). A
// single-consume-per-UP semantic is NOT asserted here: ASSUMPTION mapped to
// the coder/run decision (see contract T5.md).
bool check_up_replay_fires_again_per_up() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPReplay, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPReplay, 150.0f, 140.0f), tree);
  const ArenaAction claim1 = ClaimTap(arena, kPReplay, kButtonId);
  if (!claim1.claimed || !FireTapHandler(kButtonId)) {
    std::printf("  detail: setup broken, expected first fire true\n");
    return false;
  }
  // Second UP for the same gesture re-claims and fires again per UP.
  const ArenaAction claim2 = ClaimTap(arena, kPReplay, kButtonId);
  if (!claim2.claimed) {
    std::printf("  detail: expected re-claim on replayed UP\n");
    return false;
  }
  if (!FireTapHandler(kButtonId)) {
    std::printf("  detail: expected repeat fire on replayed UP\n");
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
  RUN_CASE(check_fire_negative_node_returns_false);
  RUN_CASE(check_fire_forged_and_zero_ids_fail_closed);
  RUN_CASE(check_claimed_tap_fires);
  RUN_CASE(check_second_claimed_tap_fires_again);
  RUN_CASE(check_down_up_path_outside_fires_nothing);
  RUN_CASE(check_past_slop_drag_claims_nothing_and_fires_nothing);
  RUN_CASE(check_repaint_fired_button_returns_true);
  RUN_CASE(check_repaint_outside_and_forged_returns_false);
  RUN_CASE(check_repaint_leaves_live_tree_intact);
  RUN_CASE(check_populated_tree_target_fires);
  RUN_CASE(check_claim_on_arena_a_does_not_let_arena_b_fire);
  RUN_CASE(check_claimed_but_never_fired_posts_no_repaint);
  RUN_CASE(check_nan_down_origin_fails_closed);
  RUN_CASE(check_up_replay_fires_again_per_up);
  if (failures > 0) {
    std::printf("tap_handler_repaint_test: %d failing\n", failures);
    return 1;
  }
  std::printf("tap_handler_repaint_test: all passing\n");
  return 0;
}
