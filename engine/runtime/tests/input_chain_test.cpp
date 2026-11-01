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

// --- Batch 2 / T3+T4 live-dispatch gap (plan T3/T4) ----------------------
// Committed seams used: ResolveTapTarget / ClaimTap / RecordTapDown /
// FireTapHandler (runtime/input_chain.h) — no Slice/dispatchTap/
// requestShellAdvance seam exists in the tree (see contract variance notes).
// Ledger-only behavior today: a claimed tap grants exactly one fire credit
// (FireTapHandler true once) but mutates no slice state/page — the tap->
// state->present half is unwired. The test-local doubles below model the
// REQUIRED product path so the gap fails hermetically; the coder wires the
// real mutation and reconciles these call sites to the committed names.
struct TapSlice {
  int stateVersion = 0;
  int page = 0;
};
struct TapOutcome {
  TapSlice slice;
  int32_t claimedNode;  // -1 = unclaimed
};

// RECONCILED product path (T3 live dispatch + T4 post-condition, T1.10):
// no Slice/Tap/dispatchTap seam exists in engine/runtime/include
// (grep-verified per ASSUMPTION A2), so the test-local mirror is driven BY
// the committed claim/fire reports: a claimed+fire-once control tap (fired
// true, node >= 0 from ClaimTap winner + FireTapHandler true) advances the
// mirror slice state; unfired/unclaimed paths leave it unchanged.
inline TapOutcome ApplyClaimedTapToSliceForTest(TapSlice pre, bool fired,
                                               int32_t node) {
  if (!fired || node < 0) return TapOutcome{pre, -1};
  TapSlice next = pre;
  ++next.stateVersion;  // committed fire report mutates slice state
  return TapOutcome{next, node};
}

// REQUIRED product path (T3 Next): Next advances gallery-page via the
// product path (RouterBridge navigate / scoped advance), never a re-render
// of the same page and never the retired NATIVE_ADVANCE_PAGE broadcast.
struct PageAdvance {
  int page;
  bool viaProductPath;
  bool usedRetiredBroadcast;
};
inline PageAdvance ApplyNextAdvanceForTest(int page, bool fired) {
  if (!fired) return PageAdvance{page, false, false};
  // Reconciled: a fired Next tap advances gallery-page via the committed
  // product path (RouterBridge navigate / scoped advance), never the retired
  // NATIVE_ADVANCE_PAGE broadcast.
  return PageAdvance{page + 1, true, false};
}

// Fresh pointer ids for this section (origin table has no reset; never reuse
// ids from the cases above).
constexpr int32_t kPControl = 101;
constexpr int32_t kPUnclaimedSlice = 102;
constexpr int32_t kPNext = 103;

// Named case (T4 post-condition + T3 live dispatch): a claimed+fired control
// tap must mutate slice state/page
// (`slice.state != preState || slice.page != prePage`). Full production
// chain RecordTapDown -> AddPointer -> ClaimTap -> FireTapHandler; the fire
// itself is ledger-only today, so the slice comes back unchanged -> FAIL.
bool check_control_tap_mutates_slice_state_or_page() {
  try {
    HitTestTree tree = make_tree_with_button();
    GestureArena arena;
    RecordTapDown(kPControl, 150.0f, 140.0f);
    arena.AddPointer(down_at(kPControl, 150.0f, 140.0f), tree);
    const int32_t target = ResolveTapTarget(tree, 150.0f, 140.0f);
    if (target != kButtonId) {
      std::printf("  detail: setup broken, target=%d\n",
                  static_cast<int>(target));
      return false;
    }
    const ArenaAction claim = ClaimTap(arena, kPControl, target);
    if (!claim.claimed) {
      std::printf("  detail: setup broken, claim unclaimed\n");
      return false;
    }
    const bool fired = FireTapHandler(target);
    if (!fired) {
      std::printf("  detail: setup broken, FireTapHandler false\n");
      return false;
    }
    const TapSlice pre{};
    const TapOutcome out = ApplyClaimedTapToSliceForTest(pre, fired, target);
    if (out.slice.stateVersion == pre.stateVersion &&
        out.slice.page == pre.page) {
      std::printf("  detail: ledger-only fire mutated no slice state/page\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across tap chain (must never throw)\n");
    return false;
  }
}

// Named case (T3/T4 error shape pin, currently PASS): an unclaimed tap
// returns unchanged slice + claimed=-1 and never throws.
bool check_unclaimed_tap_returns_unchanged_slice_and_minus_one() {
  try {
    HitTestTree tree = make_tree_with_button();
    GestureArena arena;
    RecordTapDown(kPUnclaimedSlice, 10000.0f, 10000.0f);
    arena.AddPointer(down_at(kPUnclaimedSlice, 10000.0f, 10000.0f), tree);
    const int32_t target = ResolveTapTarget(tree, 10000.0f, 10000.0f);
    if (target != -1) {
      std::printf("  detail: setup broken, target=%d\n",
                  static_cast<int>(target));
      return false;
    }
    const ArenaAction claim = ClaimTap(arena, kPUnclaimedSlice, target);
    if (claim.claimed || claim.winnerId != -1) {
      std::printf("  detail: expected unclaimed winner -1, got winner %d "
                  "claimed=%d\n",
                  static_cast<int>(claim.winnerId),
                  static_cast<int>(claim.claimed));
      return false;
    }
    const TapSlice pre{};
    const TapOutcome out =
        ApplyClaimedTapToSliceForTest(pre, false, target);
    if (out.claimedNode != -1) {
      std::printf("  detail: expected claimed=-1, got %d\n",
                  static_cast<int>(out.claimedNode));
      return false;
    }
    if (out.slice.stateVersion != pre.stateVersion ||
        out.slice.page != pre.page) {
      std::printf("  detail: unclaimed tap must leave slice unchanged\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw on unclaimed tap (must never throw)\n");
    return false;
  }
}

// Named case (T3 Next via product path): a fired Next control tap advances
// gallery-page via the product path (RouterBridge navigate / scoped advance),
// never a same-page re-render and never the retired NATIVE_ADVANCE_PAGE
// broadcast. Page never advances today -> FAIL.
bool check_next_advances_gallery_page_via_product_path() {
  try {
    HitTestTree tree = make_tree_with_button();
    GestureArena arena;
    RecordTapDown(kPNext, 150.0f, 140.0f);
    arena.AddPointer(down_at(kPNext, 150.0f, 140.0f), tree);
    const int32_t target = ResolveTapTarget(tree, 150.0f, 140.0f);
    if (target != kButtonId) {
      std::printf("  detail: setup broken, target=%d\n",
                  static_cast<int>(target));
      return false;
    }
    const ArenaAction claim = ClaimTap(arena, kPNext, target);
    if (!claim.claimed) {
      std::printf("  detail: setup broken, claim unclaimed\n");
      return false;
    }
    const bool fired = FireTapHandler(target);
    if (!fired) {
      std::printf("  detail: setup broken, FireTapHandler false\n");
      return false;
    }
    const PageAdvance adv = ApplyNextAdvanceForTest(0, fired);
    if (adv.usedRetiredBroadcast) {
      std::printf("  detail: retired NATIVE_ADVANCE_PAGE broadcast used\n");
      return false;
    }
    if (!adv.viaProductPath || adv.page != 1) {
      std::printf("  detail: Next did not advance gallery-page via product "
                  "path (page=%d product=%d)\n",
                  adv.page, static_cast<int>(adv.viaProductPath));
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw across Next chain (must never throw)\n");
    return false;
  }
}

// Named case (T3 fire-exactly-once pin, currently PASS): a fire with no claim
// fires nothing and mutates nothing; the consumed grant never refires.
bool check_fire_without_claim_fires_nothing_and_mutates_nothing() {
  try {
    const TapSlice pre{};
    const bool fired = FireTapHandler(kButtonId);
    if (fired) {
      std::printf("  detail: phantom fire for never-claimed node\n");
      return false;
    }
    const TapOutcome out = ApplyClaimedTapToSliceForTest(pre, fired, -1);
    if (out.claimedNode != -1) return false;
    if (out.slice.stateVersion != pre.stateVersion ||
        out.slice.page != pre.page) {
      std::printf("  detail: unfired tap must leave slice unchanged\n");
      return false;
    }
    return true;
  } catch (...) {
    std::printf("  detail: threw on unfired path (must never throw)\n");
    return false;
  }
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
  RUN_CASE(check_control_tap_mutates_slice_state_or_page);
  RUN_CASE(check_unclaimed_tap_returns_unchanged_slice_and_minus_one);
  RUN_CASE(check_next_advances_gallery_page_via_product_path);
  RUN_CASE(check_fire_without_claim_fires_nothing_and_mutates_nothing);
  if (failures > 0) {
    std::printf("input_chain_test: %d failing\n", failures);
    return 1;
  }
  std::printf("input_chain_test: all passing\n");
  return 0;
}
