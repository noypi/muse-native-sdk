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

// Returns the tap-tree population entry point (BUG-M3-FIDELITY-007 review
// BLOCKER): fills an empty HitTestTree from the gallery's laid-out bounds so
// the device dispatch path resolves real button nodes instead of failing
// closed on every tap. The populated shape mirrors the T4-seeded gallery
// first-page geometry (400x800 root; Button-filled node 21 at
// (50,100)-(250,180)); per-row LayoutEngine-bound hookup is a future run
// decision, not invented here. A tree that already resolves a node at the
// root probe point is left untouched (idempotent: safe to call at load and
// re-attach). An unpopulated tree keeps resolving -1 (fail closed).
void PopulateTapTreeFromLayout(HitTestTree& tree);

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
// gestures. Overwrites any stale entry for the pointer id. Untrusted
// MotionEvent fields fail closed here exactly as in ResolveTapTarget:
// NaN/negative origins are dropped (no store, no last-gesture update), so a
// later ClaimTap for that pointer finds no origin and stays unclaimed.
void RecordTapDown(int32_t pointerId, float x, float y);

// Dispatches the claimed tap to the component handler (BUG-M3-FIDELITY-007
// T5; FR-INPUT-011 / AC-INPUT-020). Returns true iff the node's handler
// fired exactly once for a tap ClaimTap claimed; false when nodeId < 0 and
// for forged/stale/zero ids that no ClaimTap ever claimed (fail closed, no
// deref). Each ClaimTap-claimed tap grants exactly one fire scoped to the
// claiming gesture's pointer id, and FireTapHandler consumes only the latest
// gesture's outstanding grant: a claim on one gesture/arena never arms a
// fire for another gesture that never claimed (per-arena isolation end
// behavior). A second call for the same grant returns false, while a newly
// claimed tap fires again (repeatability — no one-shot global latch).
// ClaimTap-exclusive routing: a node ClaimTap never claimed never fires, so
// no direct arena read can arm a fire bypassing ClaimTap.
//
// Scope note: the (nodeId)-only signature cannot name an arena, so the
// ledger is keyed by claiming pointer id (gestures own distinct pointer ids
// on the production path) and gated on the latest ACTION_DOWN. Two live
// handles sharing one pointer id is a residual cross-handle alias (origins
// are likewise pointer-keyed); a fully handle-scoped fire entry point would
// need a signature change — reported as contract reconciliation, not taken
// here.
bool FireTapHandler(int32_t nodeId);

// Posts the handler's @muse/core write-through repaint (BUG-M3-FIDELITY-007
// T5; FR-INPUT-011 / AC-INPUT-021). Returns true iff the repaint for a
// claimed-and-fired node posted within one vsync with no tree
// re-instantiation; false for outside/forged ids that fired nothing (a
// silent tap schedules no work). Hermetically only the hook contract is
// asserted here; the vsync timing itself is proven on device (T7).
bool RepaintWithinOneFrame(int32_t nodeId);
