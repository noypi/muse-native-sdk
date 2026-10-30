// Device tap chain wiring (BUG-M3-FIDELITY-007 T4; FR-INPUT-011 /
// AC-INPUT-020). Implements runtime/input_chain.h over the consume-only
// engine/input units: HitTestTree::HitTest (depth-wins) for target
// resolution, GestureArena::AddPointer/Claim for tap-vs-scroll ownership.
// Never touches the Host touch double (runtime/input.h): that rect/counter
// path is T6's to delete, and no device-path caller may reference it.

#include "runtime/input_chain.h"

#include <cmath>
#include <mutex>

#include "runtime/input.h"

namespace {

// Bounded per-pointer ACTION_DOWN origin table, sized like the arena's own
// pointer store (GestureArena::kMaxPointers = 32) so it cannot grow without
// bound across gestures. Keyed by pointer id only — never by arena — so a
// reused MotionEvent pointer id resolves against its latest recorded down
// (see RecordTapDown); there is no cross-pointer logic here.
//
// Lifetime: entries are (over)written by every gesture's ACTION_DOWN via
// RecordTapDown, which T5 calls before AddPointer on the production path, so
// a ClaimTap on the device path always reads a fresh origin for its gesture;
// a stale entry can only survive for a pointer id that never sees another
// ACTION_DOWN, in which case no new gesture (and no ClaimTap) exists for it.
// This is the documented equivalent of arena-lifetime scoping: origins are
// always refreshed at gesture start, never read stale.
//
// Thread safety: guarded by g_mu. Production dispatch (onTouchEvent) is
// normally single-threaded, so the mutex is uncontended there; it exists so
// any off-thread dispatch or test interleaving cannot corrupt the table.
struct DownOrigin {
  bool used = false;
  int32_t pointerId = 0;
  float x = 0.0f;
  float y = 0.0f;
};

constexpr int kMaxOrigins = 32;
DownOrigin g_downOrigins[kMaxOrigins];
int g_evictNext = 0;
std::mutex g_mu;

DownOrigin* FindOriginLocked(int32_t pointerId) {
  for (auto& slot : g_downOrigins) {
    if (slot.used && slot.pointerId == pointerId) {
      return &slot;
    }
  }
  return nullptr;
}

DownOrigin* StoreOriginLocked(int32_t pointerId, float x, float y) {
  if (DownOrigin* hit = FindOriginLocked(pointerId)) {
    hit->x = x;
    hit->y = y;
    return hit;
  }
  for (auto& slot : g_downOrigins) {
    if (!slot.used) {
      slot.used = true;
      slot.pointerId = pointerId;
      slot.x = x;
      slot.y = y;
      return &slot;
    }
  }
  // Table full: evict oldest (round-robin), mirroring the fixed-capacity
  // style of the consume-only arena rather than growing the heap.
  DownOrigin& slot = g_downOrigins[g_evictNext];
  g_evictNext = (g_evictNext + 1) % kMaxOrigins;
  slot.used = true;
  slot.pointerId = pointerId;
  slot.x = x;
  slot.y = y;
  return &slot;
}

}  // namespace

int32_t ResolveTapTarget(const HitTestTree& tree, float x, float y) {
  // Untrusted MotionEvent fields fail closed before touching the tree.
  if (!(x >= 0.0f) || !(y >= 0.0f)) {
    return -1;
  }
  return tree.HitTest(x, y);
}

ArenaAction ClaimTap(GestureArena& arena, int32_t pointerId, int32_t targetId) {
  // Outside tap: silent, no claim, and no arena mutation (Claim would force
  // claimed=true, which must never happen for target -1).
  if (targetId < 0) {
    return ArenaAction{-1, 0.0f, 0.0f, false};
  }
  // Fail-closed-when-absent, decided BEFORE touching the arena: a ClaimTap
  // with no prior RecordTapDown for this pointer returns unclaimed and
  // performs no arena mutation and no origin-table write. The old first-
  // sighting seeding fallback is gone: it let AddPointer-only callers claim.
  float originX = 0.0f;
  float originY = 0.0f;
  {
    std::lock_guard<std::mutex> lock(g_mu);
    DownOrigin* origin = FindOriginLocked(pointerId);
    if (origin == nullptr) {
      return ArenaAction{-1, 0.0f, 0.0f, false};
    }
    originX = origin->x;
    originY = origin->y;
  }
  // The plan chain's Claim step: also the only position read the
  // consume-only arena offers (GestureArena exposes AddPointer/Claim only).
  // Ordering: the absent-origin check above already ran before any arena
  // mutation. The slop verdict below converts a past-slop Claim into an
  // unclaimed return so the tap never fires; note the consume-only arena has
  // no un-claim operation, so its stored claimed flag (pre-marked by the
  // production path's AddPointer) is not rewound here — the tap claim itself
  // is what stays unclaimed, and a follow-up ClaimTap re-derives the same
  // slop displacement and stays unclaimed.
  ArenaAction current = arena.Claim(targetId, pointerId);
  if (!current.claimed) {
    return current;
  }
  const float dx = current.offsetX - originX;
  const float dy = current.offsetY - originY;
  // Existing slop semantics (runtime/input.h kTouchSlopPx, the sole slop
  // source of truth): magnitude at/over slop is a scroll — the tap never
  // fires.
  if (std::hypot(dx, dy) >= ::runtime::touch_detail::kTouchSlopPx) {
    return ArenaAction{-1, 0.0f, 0.0f, false};
  }
  return current;
}

void RecordTapDown(int32_t pointerId, float x, float y) {
  std::lock_guard<std::mutex> lock(g_mu);
  StoreOriginLocked(pointerId, x, y);
}
