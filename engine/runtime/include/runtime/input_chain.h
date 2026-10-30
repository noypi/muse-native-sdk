#pragma once

#include <cstdint>

#include "gesture_arena.h"
#include "hit_test.h"

// Device tap chain wiring (BUG-M3-FIDELITY-007 T4; FR-INPUT-011 /
// AC-INPUT-020): native hit-test over real laid-out bounds plus
// gesture-arena claim on the device path. Consumes engine/input
// HitTestTree/GestureArena (consume-only: this header never re-declares or
// alters their units); the slop source of truth stays
// runtime::touch_detail::kTouchSlopPx (plan A2).
//
// Chain position: JNI nativeDispatchTouch (T3) feeds raw MotionEvent fields
// through input::NormalizeMotion into GestureArena::AddPointer; the winning
// tap is decided here, then T5 routes it to the component handler.

// Returns the deepest laid-out node id at (x, y), or -1 when outside every
// bound (including the empty tree). Negative coordinates are untrusted
// MotionEvent fields and fail closed to -1.
int32_t ResolveTapTarget(const HitTestTree& tree, float x, float y);

// Returns the tap claim for pointerId against targetId: claimed with
// winner == targetId when the gesture stayed within touch slop, unclaimed
// otherwise (past-slop drag scrolls and never fires; unknown pointer and
// outside target fail closed with no phantom winner). Implemented through
// the plan chain NormalizeMotion -> GestureArena::AddPointer ->
// GestureArena::Claim; slop displacement is measured against the gesture's
// ACTION_DOWN origin.
//
// Offset bookkeeping: the consume-only arena keeps only the latest absolute
// position per pointer, so the down origin is tracked here in a bounded
// per-pointer table (GestureArena-sized: 32 slots, oldest evicted). The
// table is seeded one way only:
//   Production: T5 calls RecordTapDown at ACTION_DOWN, which (over)-
//   writes the origin for that pointer id (latest-wins on pointer-id reuse).
// A ClaimTap with no prior RecordTapDown for that pointer fails closed
// (unclaimed, no arena mutation, no table write) — there is deliberately no
// first-sighting seeding fallback and no multi-pointer winner-lock logic
// here (plan Risks: open run decision, not invented).
//
// Lifetime: every production gesture starts with RecordTapDown before
// AddPointer, so ClaimTap always reads a fresh origin; this per-gesture
// refresh is the documented equivalent of arena-lifetime scoping.
// Thread safety: the table is mutex-guarded; production dispatch
// (onTouchEvent) is normally single-threaded, so the mutex is uncontended
// there and exists to keep any off-thread use safe.
ArenaAction ClaimTap(GestureArena& arena, int32_t pointerId, int32_t targetId);

// Records the ACTION_DOWN origin for pointerId. T5's dispatch must call this
// on ACTION_DOWN (before AddPointer) so ClaimTap measures slop displacement
// from the true gesture start even when pointer ids are reused across
// gestures. Overwrites any stale entry for the pointer id.
void RecordTapDown(int32_t pointerId, float x, float y);
