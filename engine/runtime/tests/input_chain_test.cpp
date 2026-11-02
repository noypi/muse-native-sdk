// Batch 2 / T4 contract tests — FR-INPUT-011 / AC-INPUT-020 (device tap
// chain, native hit-test + gesture-arena wiring half).
// Binds ONLY to the T4 `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed engine headers only; no device, no network, no
// env/config reads, no filesystem, no live services. HitTestTree and
// GestureArena are consume-only: their own units are NOT re-tested here,
// only the new wiring contract (ResolveTapTarget / ClaimTap / RecordTapDown)
// over real laid-out bounds (never the `touch_detail` 100x100 double rect).
//
// Fail-closed-when-absent: ClaimTap with NO prior RecordTapDown for that
// pointer MUST return unclaimed — it must never seed an origin from the
// arena and claim on first sighting (that fail-open seeding is the T4
// review finding this file guards against).
//
// Production path: every claimed/unclaimed slop case drives the device
// order RecordTapDown (ACTION_DOWN) -> AddPointer (NormalizeMotion event)
// -> ClaimTap. Cases that skip RecordTapDown expect fail-closed unclaimed.
//
// Isolation: the down-origin table is process-global with no reset API, so
// every claim case uses a pointer id unique to that case (kP* below). No
// case reads another case's origin; reordering cases cannot change results.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "runtime/input_chain.h"

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

namespace {

constexpr int32_t kRootId = 10;
constexpr int32_t kButtonId = 21;
constexpr int32_t kChildId = 22;

// Unique pointer id per claim case: isolation against the process-global
// down-origin table (no reset API). Never reuse one across cases.
constexpr int32_t kPTap = 11;
constexpr int32_t kPDrag = 12;
constexpr int32_t kPJitter = 13;
constexpr int32_t kPReuse = 14;
constexpr int32_t kPAbsent = 15;    // AddPointer but NEVER RecordTapDown
constexpr int32_t kPUnknown = 999;  // never AddPointer'd, never recorded
constexpr int32_t kPOutside = 17;
constexpr int32_t kPSweepBase = 21;
constexpr int32_t kPFireOnce = 29;
constexpr int32_t kPScrim = 30;
constexpr int32_t kPSeededOutside = 31;
// T5 (security-review batch): unique ids for the five hardening cases.
constexpr int32_t kPExtreme = 41;
constexpr int32_t kPFallbackProbe = 42;
constexpr int32_t kPShrinkProbe = 43;
constexpr int32_t kPExhaustNew = 44;
constexpr int32_t kPExhaustOld = 45;
constexpr int32_t kPScrimPin = 46;
constexpr int32_t kPInfOrigin = 47;

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

// Named case: NaN coordinates resolve -1 (untrusted floats fail closed;
// must not compare-address a node or trap).
bool check_resolve_nan_coords_returns_minus_one() {
  HitTestTree tree = make_tree_with_button();
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const int32_t got = ResolveTapTarget(tree, nan, 140.0f);
  if (got != -1) {
    std::printf("  detail: expected -1 for NaN x, got %d\n",
                static_cast<int>(got));
    return false;
  }
  const int32_t got2 = ResolveTapTarget(tree, 150.0f, nan);
  if (got2 != -1) {
    std::printf("  detail: expected -1 for NaN y, got %d\n",
                static_cast<int>(got2));
    return false;
  }
  return true;
}

// Named case (production path): ACTION_DOWN recorded, down with no drag
// claims its resolved target as winner exactly once.
bool check_claim_tap_after_down_returns_claimed_winner() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPTap, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPTap, 150.0f, 140.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPTap, kButtonId);
  if (!got.claimed || got.winnerId != kButtonId) {
    std::printf("  detail: expected claimed winner %d, got winner %d "
                "claimed=%d\n",
                static_cast<int>(kButtonId), static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case (production path): a drag past slop (48px >> kTouchSlopPx 8px)
// scrolls and never fires the tap: the tap claim comes back unclaimed.
// ASSUMPTION (intended behavior, pending coder fix): a past-slop ClaimTap
// must also leave no claim behind in the arena. No arena state query is
// exposed (AddPointer/Claim only), and a follow-up ClaimTap re-reads the
// same slop displacement, so only the return is asserted here; full
// arena-cleanliness (Claim-vs-slop ordering: slop-exceeded taps must not
// leave the arena claimed) is the coder's target, not asserted state.
bool check_claim_tap_past_slop_drag_is_unclaimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPDrag, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPDrag, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPDrag, 150.0f, 188.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPDrag, kButtonId);
  if (got.claimed) {
    std::printf("  detail: expected unclaimed after 48px drag, got winner "
                "%d claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  // A second claim attempt for the same gesture must also stay unclaimed:
  // the drag owns the gesture, the tap never fires.
  const ArenaAction again = ClaimTap(arena, kPDrag, kButtonId);
  if (again.claimed) {
    std::printf("  detail: expected follow-up claim unclaimed, got winner "
                "%d claimed=%d\n",
                static_cast<int>(again.winnerId),
                static_cast<int>(again.claimed));
    return false;
  }
  return true;
}

// Named case (production path): sub-slop jitter (1px < kTouchSlopPx 8px)
// keeps the tap claimed on its target (ASSUMPTION: A2 slop source of truth;
// see contract).
bool check_claim_tap_sub_slop_jitter_stays_claimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPJitter, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPJitter, 150.0f, 140.0f), tree);
  arena.AddPointer(down_at(kPJitter, 150.0f, 141.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPJitter, kButtonId);
  if (!got.claimed || got.winnerId != kButtonId) {
    std::printf("  detail: expected claimed winner %d after 1px jitter, got "
                "winner %d claimed=%d\n",
                static_cast<int>(kButtonId), static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case (production path): a reused MotionEvent pointer id resolves
// against its LATEST recorded down (RecordTapDown overwrites the stale
// entry). Stale origin (0,0) is ~212px from the gesture; latest origin
// (150,140) is 1px away, so latest-wins claims while stale-wins would trip
// slop and return unclaimed.
bool check_claim_tap_pointer_id_reuse_latest_origin_wins() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPReuse, 0.0f, 0.0f);
  RecordTapDown(kPReuse, 150.0f, 140.0f);
  arena.AddPointer(down_at(kPReuse, 150.0f, 141.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPReuse, kButtonId);
  if (!got.claimed || got.winnerId != kButtonId) {
    std::printf("  detail: expected claimed winner %d (latest origin), got "
                "winner %d claimed=%d\n",
                static_cast<int>(kButtonId), static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case (fail-closed-when-absent): AddPointer drove the arena but NO
// RecordTapDown was ever seen for this pointer, so ClaimTap MUST return
// unclaimed. Seeding an origin from the arena's current position and
// returning claimed (the old fail-open) is the defect this case pins.
bool check_claim_tap_without_recorded_down_is_unclaimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  arena.AddPointer(down_at(kPAbsent, 150.0f, 140.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPAbsent, kButtonId);
  if (got.claimed) {
    std::printf("  detail: expected unclaimed with no RecordTapDown, got "
                "winner %d claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// Named case: a never-seen pointer claims nothing (fail closed: no phantom
// winner for an untracked MotionEvent pointer id).
bool check_claim_tap_unknown_pointer_is_unclaimed() {
  GestureArena arena;
  const ArenaAction got = ClaimTap(arena, kPUnknown, kButtonId);
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
// Own origin recorded (production order) so the case is self-contained;
// ClaimTap short-circuits on target < 0 before any origin read.
bool check_claim_tap_outside_target_is_unclaimed() {
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  RecordTapDown(kPOutside, 10000.0f, 10000.0f);
  arena.AddPointer(down_at(kPOutside, 10000.0f, 10000.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (target != -1) return false;
  const ArenaAction got = ClaimTap(arena, kPOutside, target);
  if (got.claimed) {
    std::printf("  detail: expected unclaimed for outside target, got winner "
                "%d claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  return true;
}

// ---- BUG-INPUT-002 T1 regression: out-of-hardcoded-400x800 taps ----
// Seeded via the public PopulateTapTreeFromLayout entry point (never a
// hand-built rect): the sweep mirrors dp_t8c_sweep.log (x=540, y=90..1690).
// Red on the hardcoded 400x800 seeder (x>400 or y>800 resolves -1); green
// once T2 seeds from real laid-out bounds/viewport. Hermetic: C++ only,
// no device/downloads/secrets. Node-20 scrim routing preserved (depth-wins
// root 10 < scrim 20 < content 21).

// All in-bounds sweep taps resolve to a real node (>= 0).
bool check_seeded_sweep_resolves_inside() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const float ys[] = {90.0f, 500.0f, 1000.0f, 1400.0f, 1690.0f};
  for (float y : ys) {
    const int32_t got = ResolveTapTarget(tree, 540.0f, y);
    if (got < 0) {
      std::printf("  detail: sweep (540,%g) expected >= 0, got %d\n",
                  static_cast<double>(y), static_cast<int>(got));
      return false;
    }
  }
  return true;
}

// Each sweep tap claims and fires exactly once (unique pointer per tap
// against the process-global origin/fire tables).
bool check_seeded_sweep_claim_fire_exactly_once() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const float ys[] = {90.0f, 500.0f, 1000.0f, 1400.0f, 1690.0f};
  for (int i = 0; i < 5; ++i) {
    const int32_t ptr = kPSweepBase + i;
    const float y = ys[i];
    RecordTapDown(ptr, 540.0f, y);
    GestureArena arena;
    arena.AddPointer(down_at(ptr, 540.0f, y), tree);
    const int32_t target = ResolveTapTarget(tree, 540.0f, y);
    if (target < 0) {
      std::printf("  detail: sweep (540,%g) resolve failed, got -1\n",
                  static_cast<double>(y));
      return false;
    }
    const ArenaAction got = ClaimTap(arena, ptr, target);
    if (!got.claimed || got.winnerId != target) {
      std::printf("  detail: sweep (540,%g) expected claimed winner %d, got "
                  "winner %d claimed=%d\n",
                  static_cast<double>(y), static_cast<int>(target),
                  static_cast<int>(got.winnerId),
                  static_cast<int>(got.claimed));
      return false;
    }
    if (!FireTapHandler(target)) {
      std::printf("  detail: sweep (540,%g) FireTapHandler false on first "
                  "fire\n",
                  static_cast<double>(y));
      return false;
    }
    if (FireTapHandler(target)) {
      std::printf("  detail: sweep (540,%g) FireTapHandler true twice "
                  "(must fire exactly once)\n",
                  static_cast<double>(y));
      return false;
    }
  }
  return true;
}

// Representative out-of-400x800 tap fires exactly once, second fire silent.
// (Single-point form of the sweep; pins the ClaimTap+FireTapHandler once
// contract at (540,1000): x > 400 and y > 800 on the old seeder.)
bool check_seeded_out_of_400x800_fires_exactly_once() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  RecordTapDown(kPFireOnce, 540.0f, 1000.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPFireOnce, 540.0f, 1000.0f), tree);
  const int32_t target = ResolveTapTarget(tree, 540.0f, 1000.0f);
  if (target < 0) {
    std::printf("  detail: (540,1000) expected >= 0, got -1\n");
    return false;
  }
  const ArenaAction got = ClaimTap(arena, kPFireOnce, target);
  if (!got.claimed || got.winnerId != target) return false;
  if (!FireTapHandler(target)) return false;
  if (FireTapHandler(target)) {
    std::printf("  detail: second FireTapHandler must be false\n");
    return false;
  }
  return true;
}

// Seeded scrim routing preserved: content point hits 21, scrim-only point
// hits 20 (depth-wins root 10 < scrim 20 < content 21); scrim tap claims
// and fires its own node exactly once (not content).
bool check_seeded_scrim_routing_preserved() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const int32_t button = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (button != 21) {
    std::printf("  detail: button (150,140) expected 21, got %d\n",
                static_cast<int>(button));
    return false;
  }
  const int32_t scrim = ResolveTapTarget(tree, 300.0f, 400.0f);
  if (scrim != 20) {
    std::printf("  detail: scrim (300,400) expected 20, got %d\n",
                static_cast<int>(scrim));
    return false;
  }
  RecordTapDown(kPScrim, 300.0f, 400.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPScrim, 300.0f, 400.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPScrim, scrim);
  if (!got.claimed || got.winnerId != 20) {
    std::printf("  detail: scrim claim expected winner 20, got %d "
                "claimed=%d\n",
                static_cast<int>(got.winnerId),
                static_cast<int>(got.claimed));
    return false;
  }
  if (!FireTapHandler(20)) {
    std::printf("  detail: scrim FireTapHandler(20) false\n");
    return false;
  }
  return true;
}

// Seeded tree: far-outside tap resolves -1, claims nothing, fires nothing.
bool check_seeded_outside_stays_silent() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const int32_t target = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (target != -1) {
    std::printf("  detail: outside expected -1, got %d\n",
                static_cast<int>(target));
    return false;
  }
  RecordTapDown(kPSeededOutside, 10000.0f, 10000.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPSeededOutside, 10000.0f, 10000.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPSeededOutside, target);
  if (got.claimed) {
    std::printf("  detail: outside claim must stay unclaimed\n");
    return false;
  }
  if (FireTapHandler(target)) {
    std::printf("  detail: outside FireTapHandler must be false\n");
    return false;
  }
  return true;
}

// ---- T5 security-review hardening (FR-INPUT-011/AC-INPUT-020) ----
// Binds ONLY to the T2 `Interface:` line (viewport seeder + ResolveTapTarget
// fail-closed contract); hermetic, in-process, no device/downloads/secrets.

// Shrink reseed denies stale-rect fire: seed 1080x1920, reseed 400x800, then
// a tap at (540,1000) — inside the OLD rect but outside the NEW bounds —
// must resolve -1, claim nothing, and fire nothing.
bool check_shrink_reseed_denies_stale_rect_fire() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  PopulateTapTreeFromLayout(tree, 400.0f, 800.0f);
  const int32_t target = ResolveTapTarget(tree, 540.0f, 1000.0f);
  if (target != -1) {
    std::printf("  detail: post-shrink (540,1000) expected -1, got %d\n",
                static_cast<int>(target));
    return false;
  }
  RecordTapDown(kPShrinkProbe, 540.0f, 1000.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPShrinkProbe, 540.0f, 1000.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPShrinkProbe, target);
  if (got.claimed) {
    std::printf("  detail: post-shrink claim must stay unclaimed\n");
    return false;
  }
  if (FireTapHandler(target)) {
    std::printf("  detail: post-shrink FireTapHandler must be false\n");
    return false;
  }
  return true;
}

// kMaxNodes exhaustion stays bounded and fail-closed: 30 distinct growing
// viewports (each appends 3 nodes; cap is 64, so later appends partially or
// fully drop, silently). A tap in newest-only coverage must resolve -1 and
// stay silent (no stale fire, no phantom id), while the oldest geometry keeps
// serving its button claim+fire exactly once (drops fail closed, never
// corrupt the live tree).
bool check_kmax_exhaustion_new_coverage_stays_silent() {
  HitTestTree tree;
  for (int i = 0; i < 30; ++i) {
    PopulateTapTreeFromLayout(tree, 120.0f + 40.0f * i, 240.0f + 80.0f * i);
  }
  // Inside V30 (1280x2560) but outside V22 (960x1920, the last fully
  // applied root): genuinely new coverage the exhausted tree never stored.
  const int32_t target = ResolveTapTarget(tree, 1000.0f, 2000.0f);
  if (target != -1) {
    std::printf("  detail: exhausted-tree new-area tap expected -1, got %d\n",
                static_cast<int>(target));
    return false;
  }
  RecordTapDown(kPExhaustNew, 1000.0f, 2000.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPExhaustNew, 1000.0f, 2000.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPExhaustNew, target);
  if (got.claimed) {
    std::printf("  detail: exhausted-tree new-area claim must stay unclaimed\n");
    return false;
  }
  if (FireTapHandler(target)) {
    std::printf("  detail: exhausted-tree new-area fire must be false\n");
    return false;
  }
  // Oldest geometry still serves: button (50,100,200,80) claims+fires once.
  RecordTapDown(kPExhaustOld, 150.0f, 140.0f);
  GestureArena arena2;
  arena2.AddPointer(down_at(kPExhaustOld, 150.0f, 140.0f), tree);
  const int32_t oldTarget = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (oldTarget != kButtonId) {
    std::printf("  detail: post-exhaustion button expected %d, got %d\n",
                static_cast<int>(kButtonId), static_cast<int>(oldTarget));
    return false;
  }
  const ArenaAction oldGot = ClaimTap(arena2, kPExhaustOld, oldTarget);
  if (!oldGot.claimed || oldGot.winnerId != kButtonId) return false;
  if (!FireTapHandler(oldTarget)) return false;
  if (FireTapHandler(oldTarget)) {
    std::printf("  detail: post-exhaustion button must fire exactly once\n");
    return false;
  }
  return true;
}

// Untrusted coordinate extremes fail closed: +Inf / -Inf / huge-finite /
// negative taps resolve -1 with no claim, no fire, and no ledger write
// (a dropped RecordTapDown leaves no origin, so the same pointer stays
// unclaimed; a later valid down on that pointer still claims+fires once,
// proving the ledger was not poisoned).
bool check_extreme_coords_rejected_no_claim_no_fire() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const float inf = std::numeric_limits<float>::infinity();
  const float cases[][2] = {
      {inf, 140.0f}, {150.0f, -inf}, {-inf, -inf}, {1.0e30f, 1.0e30f},
      {-1.0f, 140.0f}, {150.0f, -1.0f},
  };
  for (const auto& c : cases) {
    const int32_t got = ResolveTapTarget(tree, c[0], c[1]);
    if (got != -1) {
      std::printf("  detail: extreme (%g,%g) expected -1, got %d\n",
                  static_cast<double>(c[0]), static_cast<double>(c[1]),
                  static_cast<int>(got));
      return false;
    }
  }
  // Dropped down-origin leaves no ledger write: a NaN origin is dropped, so
  // the claim stays unclaimed and nothing fires.
  RecordTapDown(kPExtreme, std::numeric_limits<float>::quiet_NaN(), 140.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPExtreme, inf, 140.0f), tree);
  if (ClaimTap(arena, kPExtreme, kButtonId).claimed) {
    std::printf("  detail: extreme-origin claim must stay unclaimed\n");
    return false;
  }
  if (FireTapHandler(kButtonId)) {
    std::printf("  detail: extreme-origin fire must be false\n");
    return false;
  }
  // Same pointer recovers: a later valid down claims+fires exactly once.
  RecordTapDown(kPExtreme, 150.0f, 140.0f);
  GestureArena arena2;
  arena2.AddPointer(down_at(kPExtreme, 150.0f, 140.0f), tree);
  const ArenaAction got = ClaimTap(arena2, kPExtreme, kButtonId);
  if (!got.claimed || got.winnerId != kButtonId) {
    std::printf("  detail: recovered pointer must claim button\n");
    return false;
  }
  if (!FireTapHandler(kButtonId)) {
    std::printf("  detail: recovered pointer must fire once\n");
    return false;
  }
  return true;
}

// +Inf down-origin must fail closed: RecordTapDown currently admits +Inf
// (`!(x>=0)` is true for +Inf), and a matching +Inf move makes the slop
// displacement NaN (inf-inf), which defeats the `>= kTouchSlopPx` check and
// claims. This case pins the requested control (unclaimed, no fire); it is
// RED against the committed seeder — routes to coder (reject non-finite in
// RecordTapDown, mirroring ViewportUsable).
bool check_inf_origin_claim_stays_unclaimed() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const float inf = std::numeric_limits<float>::infinity();
  RecordTapDown(kPInfOrigin, inf, 140.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPInfOrigin, inf, 140.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPInfOrigin, kButtonId);
  if (got.claimed) {
    std::printf("  detail: +Inf-origin claim must stay unclaimed\n");
    return false;
  }
  if (FireTapHandler(kButtonId)) {
    std::printf("  detail: +Inf-origin fire must be false\n");
    return false;
  }
  return true;
}

// Fallback seed serves taps before the first frame, stays silent outside:
// pre-frame tree (zero-arg 1080x1920 seed) claims+fires (540,1690) exactly
// once, while far-outside (10000,10000) resolves -1 with no claim/no fire.
bool check_fallback_seed_serves_inside_silent_outside() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const int32_t inside = ResolveTapTarget(tree, 540.0f, 1690.0f);
  if (inside < 0) {
    std::printf("  detail: fallback (540,1690) expected >= 0, got -1\n");
    return false;
  }
  RecordTapDown(kPFallbackProbe, 540.0f, 1690.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPFallbackProbe, 540.0f, 1690.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPFallbackProbe, inside);
  if (!got.claimed || got.winnerId != inside) {
    std::printf("  detail: fallback inside tap must claim winner %d\n",
                static_cast<int>(inside));
    return false;
  }
  if (!FireTapHandler(inside) || FireTapHandler(inside)) {
    std::printf("  detail: fallback inside tap must fire exactly once\n");
    return false;
  }
  const int32_t outside = ResolveTapTarget(tree, 10000.0f, 10000.0f);
  if (outside != -1) {
    std::printf("  detail: fallback outside expected -1, got %d\n",
                static_cast<int>(outside));
    return false;
  }
  if (FireTapHandler(outside)) {
    std::printf("  detail: fallback outside fire must be false\n");
    return false;
  }
  return true;
}

// target == 20 scrim routing stays pinned: a button tap resolves 21 (never
// the scrim dismiss node), a scrim-only tap resolves 20 and claims+fires its
// own node exactly once (not content), and an unclaimed 21 never fires
// (no ClaimTap bypass). Layering-page vs non-layering dispatch (`target ==
// 20` + IsLayeringPageSlice/onScrimTap in jni_bridge) is JNI/device scope —
// EXTERNAL, verified by the T4 scrim suites on device.
bool check_scrim_routing_pinned_button_never_dismiss() {
  HitTestTree tree;
  PopulateTapTreeFromLayout(tree);
  const int32_t button = ResolveTapTarget(tree, 150.0f, 140.0f);
  if (button != kButtonId) {
    std::printf("  detail: button (150,140) expected 21, got %d\n",
                static_cast<int>(button));
    return false;
  }
  const int32_t scrim = ResolveTapTarget(tree, 300.0f, 400.0f);
  if (scrim != 20) {
    std::printf("  detail: scrim (300,400) expected 20, got %d\n",
                static_cast<int>(scrim));
    return false;
  }
  RecordTapDown(kPScrimPin, 300.0f, 400.0f);
  GestureArena arena;
  arena.AddPointer(down_at(kPScrimPin, 300.0f, 400.0f), tree);
  const ArenaAction got = ClaimTap(arena, kPScrimPin, scrim);
  if (!got.claimed || got.winnerId != 20) {
    std::printf("  detail: scrim claim expected winner 20, got %d\n",
                static_cast<int>(got.winnerId));
    return false;
  }
  if (!FireTapHandler(20) || FireTapHandler(20)) {
    std::printf("  detail: scrim must fire exactly once\n");
    return false;
  }
  // Unclaimed content never fires: no ClaimTap bypass arms node 21.
  if (FireTapHandler(kButtonId)) {
    std::printf("  detail: unclaimed button fire must be false\n");
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
  RUN_CASE(check_resolve_nan_coords_returns_minus_one);
  RUN_CASE(check_claim_tap_after_down_returns_claimed_winner);
  RUN_CASE(check_claim_tap_past_slop_drag_is_unclaimed);
  RUN_CASE(check_claim_tap_sub_slop_jitter_stays_claimed);
  RUN_CASE(check_claim_tap_pointer_id_reuse_latest_origin_wins);
  RUN_CASE(check_claim_tap_without_recorded_down_is_unclaimed);
  RUN_CASE(check_claim_tap_unknown_pointer_is_unclaimed);
  RUN_CASE(check_claim_tap_outside_target_is_unclaimed);
  RUN_CASE(check_seeded_sweep_resolves_inside);
  RUN_CASE(check_seeded_sweep_claim_fire_exactly_once);
  RUN_CASE(check_seeded_out_of_400x800_fires_exactly_once);
  RUN_CASE(check_seeded_scrim_routing_preserved);
  RUN_CASE(check_seeded_outside_stays_silent);
  RUN_CASE(check_shrink_reseed_denies_stale_rect_fire);
  RUN_CASE(check_kmax_exhaustion_new_coverage_stays_silent);
  RUN_CASE(check_extreme_coords_rejected_no_claim_no_fire);
  RUN_CASE(check_inf_origin_claim_stays_unclaimed);
  RUN_CASE(check_fallback_seed_serves_inside_silent_outside);
  RUN_CASE(check_scrim_routing_pinned_button_never_dismiss);
  if (failures > 0) {
    std::printf("input_chain_test: %d failing\n", failures);
    return 1;
  }
  std::printf("input_chain_test: all passing\n");
  return 0;
}
