// T2 contract tests — FR-LISTS-004 / AC-LISTS-019 (TC-LISTS-019).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services.
// Binds to the T2 `Interface:` lines:
//   `DragVerdict ResolveDragClaim(float dx, float dy, bool longPress, ArenaClaim arena);`
//   returns {owner: VerticalList | HorizontalParent, reason}.
//   `DropResult CommitDrop(ReorderState& s, int toIndex);`
//   (exactly one model update).
// Constrained by D1: vertical+long-press owned by the list; horizontal
// claims yield to the arena/parent. Asserted against the
// `GestureArena::Claim` semantics in `engine/input/include/gesture_arena.h`
// (read-only; the arena is modelled, not linked): an already-claimed arena
// (`ArenaAction::claimed == true`, foreign `winnerId`) defeats the list's
// claim, mirroring `Claim(winnerId, pointerId)` granting exactly one owner.
// Include path follows the engine convention:
//   #include "lists/drag_reorder.h" with engine/lists/include on the path.
//
// ASSUMPTION (type spellings): the plan `Interface:` line does not settle
// the C++ namespace, the `DragOwner` enumerators beyond their names, the
// `reason` representation, the `ArenaClaim` shape, or the `ReorderState` /
// `DropResult` shapes. This test proposes: namespace `lists`;
// `DragOwner::{VerticalList, HorizontalParent}`; `DragVerdict{DragOwner
// owner; std::string reason}`; `ArenaClaim{bool claimed; int winnerId}`
// mirroring `ArenaAction::{claimed, winnerId}`; `ReorderState{
// std::vector<std::string> order; size_t draggedFrom; int updates; }`;
// `DropResult{bool ok; int index; int updatesApplied; }`.
// Reconciliation rule: if the coder names them differently, update these
// call sites — not the expected behavior (D1 precedence, single update).
//
// ASSUMPTION (claim rule): the list wins iff `longPress && |dy| > |dx| &&
// !arena.claimed`. No long-press means no reorder session (tap/scroll, not
// a drag claim); ties and zero displacement yield conservatively to the
// parent. The AC pins the two poles (vertical+long-press vs horizontal),
// not the tie break.
//
// ASSUMPTION (drop clamping): out-of-range `toIndex` clamps into
// `[0, size]`; dropping at the dragged position is a single-update no-op in
// content. The "exactly one model update" invariant holds in all cases.
#include <cstdio>
#include <string>
#include <vector>

#include "lists/drag_reorder.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::ArenaClaim unclaimed() {
  lists::ArenaClaim a{};
  a.claimed = false;
  a.winnerId = -1;
  return a;
}

lists::ArenaClaim claimed_by_other() {
  lists::ArenaClaim a{};
  a.claimed = true;
  a.winnerId = 7;  // Foreign winner per GestureArena::Claim semantics.
  return a;
}

lists::ReorderState state_n(size_t n, size_t dragged_from) {
  lists::ReorderState s{};
  for (size_t i = 0; i < n; ++i) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "k%05zu", i);
    s.order.emplace_back(buf);
  }
  s.draggedFrom = dragged_from;
  s.updates = 0;
  return s;
}

// D1 pole 1: vertical displacement + long-press is owned by the list.
void case_vertical_longpress_owned_by_list() {
  lists::DragVerdict v =
      lists::ResolveDragClaim(/*dx=*/4.0f, /*dy=*/60.0f,
                              /*longPress=*/true, unclaimed());
  check(v.owner == lists::DragOwner::VerticalList,
        "vertical_longpress_owned_by_list");
}

// D1 pole 2: horizontal displacement yields to the arena/parent.
void case_horizontal_yields_to_parent() {
  lists::DragVerdict v =
      lists::ResolveDragClaim(/*dx=*/80.0f, /*dy=*/6.0f,
                              /*longPress=*/true, unclaimed());
  check(v.owner == lists::DragOwner::HorizontalParent,
        "horizontal_yields_to_parent");
}

// An arena already claimed by another winner defeats the list's claim,
// mirroring GestureArena::Claim granting exactly one owner.
void case_arena_claimed_yields() {
  lists::DragVerdict v =
      lists::ResolveDragClaim(/*dx=*/4.0f, /*dy=*/60.0f,
                              /*longPress=*/true, claimed_by_other());
  check(v.owner == lists::DragOwner::HorizontalParent,
        "arena_claimed_yields");
}

// Without long-press there is no reorder session to own.
void case_no_longpress_no_claim() {
  lists::DragVerdict v =
      lists::ResolveDragClaim(/*dx=*/4.0f, /*dy=*/60.0f,
                              /*longPress=*/false, unclaimed());
  check(v.owner == lists::DragOwner::HorizontalParent,
        "no_longpress_no_claim");
}

// Ambiguous ties yield conservatively to the parent.
void case_tie_yields() {
  lists::DragVerdict v =
      lists::ResolveDragClaim(/*dx=*/50.0f, /*dy=*/50.0f,
                              /*longPress=*/true, unclaimed());
  check(v.owner == lists::DragOwner::HorizontalParent, "tie_yields");
}

// Zero displacement claims nothing.
void case_zero_displacement_yields() {
  lists::DragVerdict v = lists::ResolveDragClaim(/*dx=*/0.0f, /*dy=*/0.0f,
                                                 /*longPress=*/true,
                                                 unclaimed());
  check(v.owner == lists::DragOwner::HorizontalParent,
        "zero_displacement_yields");
}

// Drop moves the dragged item and applies exactly one model update.
void case_commit_drop_single_update() {
  lists::ReorderState s = state_n(5, /*dragged_from=*/1);
  lists::DropResult r = lists::CommitDrop(s, /*toIndex=*/3);
  check(r.ok && r.updatesApplied == 1 && s.updates == 1,
        "commit_drop_single_update");
  std::vector<std::string> want = {"k00000", "k00002", "k00003", "k00001",
                                   "k00004"};
  check(s.order == want, "commit_drop_lands_at_index");
}

// TC-LISTS-019: drop at the end of a 10,000-item list is stable — one
// update, size preserved, item lands last.
void case_commit_drop_end_of_10k() {
  lists::ReorderState s = state_n(10000, /*dragged_from=*/0);
  lists::DropResult r = lists::CommitDrop(s, /*toIndex=*/9999);
  check(r.ok && r.updatesApplied == 1 && s.updates == 1,
        "drop_end_10k_single_update");
  check(s.order.size() == 10000 && s.order.back() == "k00000",
        "drop_end_10k_lands_last");
}

// Out-of-range targets clamp into range with a single update.
void case_commit_drop_clamps() {
  lists::ReorderState s = state_n(5, /*dragged_from=*/0);
  lists::DropResult r = lists::CommitDrop(s, /*toIndex=*/100);
  check(r.ok && r.updatesApplied == 1 && s.order.back() == "k00000",
        "drop_clamps_to_end");
  lists::ReorderState t = state_n(5, /*dragged_from=*/4);
  lists::DropResult r2 = lists::CommitDrop(t, /*toIndex=*/-3);
  check(r2.ok && r2.updatesApplied == 1 && t.order.front() == "k00004",
        "drop_clamps_to_start");
}

// Dropping at the dragged position changes nothing but still accounts one
// model update (the single-update invariant is unconditional).
void case_commit_drop_same_index() {
  lists::ReorderState s = state_n(5, /*dragged_from=*/2);
  std::vector<std::string> before = s.order;
  lists::DropResult r = lists::CommitDrop(s, /*toIndex=*/2);
  check(r.ok && r.updatesApplied == 1 && s.updates == 1 &&
            s.order == before,
        "drop_same_index_single_update_noop");
}

}  // namespace

int main() {
  case_vertical_longpress_owned_by_list();
  case_horizontal_yields_to_parent();
  case_arena_claimed_yields();
  case_no_longpress_no_claim();
  case_tie_yields();
  case_zero_displacement_yields();
  case_commit_drop_single_update();
  case_commit_drop_end_of_10k();
  case_commit_drop_clamps();
  case_commit_drop_same_index();
  if (g_failures == 0) {
    std::printf("ALL TESTS PASSED\n");
  } else {
    std::printf("%d TEST(S) FAILED\n", g_failures);
  }
  return g_failures == 0 ? 0 : 1;
}
