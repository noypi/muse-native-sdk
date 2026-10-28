// T2 contract tests — FR-INPUT-005 / AC-INPUT-008 (TC-INPUT-008).
// Hermetic: C++17 + input headers only; layout geometry is stubbed
// in-process (literal rects), no network, no env/config reads,
// no filesystem, no live services, no Android NDK.
// Binds to the T2 `Interface:` lines (plan.md verbatim):
//   `ArenaAction GestureArena::AddPointer(const NormalizedEvent& ev, HitTestTree& tree);`
//   `ArenaAction GestureArena::Claim(int32_t winnerId, int32_t pointerId);`
// Include path follows the engine/input convention:
//   #include "gesture_arena.h" / #include "hit_test.h" with
//   engine/input/include on the path; NormalizedEvent comes from
//   #include "input/event_model.h" (T1, already committed — read-only).
//
// ASSUMPTION (ArenaAction shape): neither the plan nor the spec settles the
// C++ shape of ArenaAction. This test proposes:
//   struct ArenaAction { int32_t winnerId; float offsetX; float offsetY; bool claimed; };
// where winnerId is the recognizer/node id currently owning the pointer,
// offsetX/offsetY is the sub-pixel drag offset accumulated for the pointer
// (must survive a parent Claim without restart), and claimed is true once a
// winner lock has been taken (by AddPointer arbitration or Claim).
// Reconciliation rule: if the coder names the type/fields differently,
// update these call sites — not the expected behavior (no lost first frame,
// no sub-pixel offset jump on parent claim).
//
// ASSUMPTION (HitTestTree shape): the plan says HitTestTree is "a
// spatial-hash tree fed by stubbed layout geometry" but settles no method
// names. This test proposes:
//   struct HitRect { float x, y, w, h; bool contains(float px, float py) const; };
//   class HitTestTree { void AddNode(int32_t id, int32_t parentId, HitRect bounds);
//                       int32_t HitTest(float x, float y) const; };
// nesting bottom-sheet(3) > horizontal-list(2) > vertical-feed(1) with ids
// 1=root feed, 2=list child of 1, 3=sheet child of 2; HitTest returns the
// deepest containing node id. Reconciliation: rename-safe per above.
//
// ASSUMPTION (GestureArena shape): class GestureArena with the two plan
// methods; winner ids are the HitTestTree node ids; pointer identity is
// NormalizedEvent::pointerId. Claim of an unknown pointerId returns
// claimed=false and preserves state. Reconciliation: rename-safe per above.
//
// ASSUMPTION (arbitration policy): the plan/spec pin no velocity or slop
// thresholds ("velocity thresholds under-specified", plan Risks). This test
// defines the executable conformance: first AddPointer frame routes to the
// deepest hit child; a parent Claim escalates ownership while preserving the
// exact accumulated sub-pixel offset. Threshold values are NOT asserted.
#include <cstdint>
#include <cstdio>

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

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

bool same_offset(float a, float b) { return a == b; }

input::NormalizedEvent make_touch(std::int32_t pointerId, float x, float y) {
  input::AxisState axes{};
  axes.x = x;
  axes.y = y;
  axes.pressure = 1.0f;
  return input::NormalizeMotion(pointerId, input::ToolType::Touch, 0u, axes,
                                0u, input::SourceClass::Touch);
}

// Nested-scroll fixture: bottom sheet (3) over horizontal list (2) over
// vertical feed (1). All rects overlap at (50,50) so the deepest hit is 3.
HitTestTree make_nested_tree() {
  HitTestTree tree;
  tree.AddNode(1, 0, HitRect{0.0f, 0.0f, 200.0f, 400.0f});
  tree.AddNode(2, 1, HitRect{0.0f, 0.0f, 200.0f, 200.0f});
  tree.AddNode(3, 2, HitRect{0.0f, 0.0f, 200.0f, 100.0f});
  return tree;
}

// AC-INPUT-008: no lost first frame — the very first AddPointer for a
// down at an overlapped point routes to the deepest child (sheet=3).
void case_first_frame_routes_to_deepest_child() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction action = arena.AddPointer(make_touch(7, 50.0f, 50.0f), tree);
  check(action.winnerId == 3, "first_frame_routes_to_deepest_child");
}

// AC-INPUT-008: continued drag frames keep accumulating offset on the
// current winner without dropping a frame (second AddPointer still owned,
// offset advances by the exact sub-pixel delta).
void case_drag_frames_accumulate_without_loss() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction first = arena.AddPointer(make_touch(7, 50.0f, 50.0f), tree);
  ArenaAction second = arena.AddPointer(make_touch(7, 50.25f, 50.0f), tree);
  check(first.winnerId == 3 && second.winnerId == 3 &&
            same_offset(second.offsetX - first.offsetX, 0.25f),
        "drag_frames_accumulate_without_loss");
}

// RULE-INPUT-001 claim escalation: parent (list=2) claims the pointer from
// the child (sheet=3); ownership moves but the accumulated sub-pixel offset
// is carried over bit-exact (no jump, no restart to zero).
void case_parent_claim_preserves_subpixel_offset() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction before = arena.AddPointer(make_touch(7, 50.125f, 60.5f), tree);
  ArenaAction claimed = arena.Claim(2, 7);
  check(claimed.claimed && claimed.winnerId == 2 &&
            same_offset(claimed.offsetX, before.offsetX) &&
            same_offset(claimed.offsetY, before.offsetY),
        "parent_claim_preserves_subpixel_offset");
}

// After a parent claim, further drag frames continue from the carried-over
// offset (no restart): the next delta applies on top of the preserved base.
void case_post_claim_drag_continues_from_preserved_offset() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction before = arena.AddPointer(make_touch(7, 50.125f, 60.5f), tree);
  ArenaAction claimed = arena.Claim(2, 7);
  ArenaAction next = arena.AddPointer(make_touch(7, 51.125f, 60.5f), tree);
  check(next.winnerId == 2 &&
            same_offset(next.offsetX - claimed.offsetX, 1.0f) &&
            same_offset(before.offsetX, claimed.offsetX),
        "post_claim_drag_continues_from_preserved_offset");
}

// Winner lock: once claimed, the loser's implicit re-entry does not steal
// the pointer — a second AddPointer for the same pointer stays with the
// claiming parent, not the original deepest child re-arbitrated.
void case_winner_lock_holds_after_claim() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  arena.AddPointer(make_touch(7, 50.0f, 50.0f), tree);
  arena.Claim(1, 7);
  ArenaAction next = arena.AddPointer(make_touch(7, 52.0f, 50.0f), tree);
  check(next.winnerId == 1 && next.claimed,
        "winner_lock_holds_after_claim");
}

// Boundary: Claim for an unknown pointerId is rejected (claimed=false) and
// does not disturb an already-tracked pointer.
void case_claim_unknown_pointer_rejected() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction first = arena.AddPointer(make_touch(7, 50.0f, 50.0f), tree);
  ArenaAction bogus = arena.Claim(2, 999);
  ArenaAction again = arena.AddPointer(make_touch(7, 50.5f, 50.0f), tree);
  check(!bogus.claimed && again.winnerId == first.winnerId,
        "claim_unknown_pointer_rejected");
}

// Boundary: pointers are independent — claiming pointer A does not move
// pointer B's winner or offset.
void case_pointers_are_independent() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction a = arena.AddPointer(make_touch(7, 50.0f, 50.0f), tree);
  ArenaAction b = arena.AddPointer(make_touch(8, 10.0f, 300.0f), tree);
  ArenaAction claimed = arena.Claim(2, 7);
  check(a.winnerId == 3 && b.winnerId == 1 && claimed.winnerId == 2 &&
            same_offset(b.offsetX, 10.0f) && same_offset(b.offsetY, 300.0f),
        "pointers_are_independent");
}

// Boundary: hit outside every node resolves to no winner (-1) rather than
// an arbitrary node; the frame is still not "lost" (a well-defined action).
void case_miss_hit_test_has_no_winner() {
  GestureArena arena;
  HitTestTree tree = make_nested_tree();
  ArenaAction action = arena.AddPointer(make_touch(9, 500.0f, 900.0f), tree);
  check(action.winnerId == -1 && !action.claimed,
        "miss_hit_test_has_no_winner");
}

}  // namespace

int main() {
  case_first_frame_routes_to_deepest_child();
  case_drag_frames_accumulate_without_loss();
  case_parent_claim_preserves_subpixel_offset();
  case_post_claim_drag_continues_from_preserved_offset();
  case_winner_lock_holds_after_claim();
  case_claim_unknown_pointer_rejected();
  case_pointers_are_independent();
  case_miss_hit_test_has_no_winner();
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
