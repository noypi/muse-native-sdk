// Device tap chain wiring (BUG-M3-FIDELITY-007 T4; FR-INPUT-011 /
// AC-INPUT-020). Implements runtime/input_chain.h over the consume-only
// engine/input units: HitTestTree::HitTest (depth-wins) for target
// resolution, GestureArena::AddPointer/Claim for tap-vs-scroll ownership.
// Never touches the Host touch double (runtime/input.h): that rect/counter
// path is T6's to delete, and no device-path caller may reference it.

#include "runtime/input_chain.h"

#include <cmath>

#include "runtime/input.h"

namespace {

// Bounded per-pointer ACTION_DOWN origin table, sized like the arena's own
// pointer store (GestureArena::kMaxPointers = 32) so it cannot grow without
// bound across gestures. Keyed by pointer id only — never by arena — so a
// reused MotionEvent pointer id resolves against its latest recorded down
// (see RecordTapDown); there is no cross-pointer logic here.
struct DownOrigin {
  bool used = false;
  int32_t pointerId = 0;
  float x = 0.0f;
  float y = 0.0f;
};

constexpr int kMaxOrigins = 32;
DownOrigin g_downOrigins[kMaxOrigins];
int g_evictNext = 0;

DownOrigin* FindOrigin(int32_t pointerId) {
  for (auto& slot : g_downOrigins) {
    if (slot.used && slot.pointerId == pointerId) {
      return &slot;
    }
  }
  return nullptr;
}

DownOrigin* StoreOrigin(int32_t pointerId, float x, float y) {
  if (DownOrigin* hit = FindOrigin(pointerId)) {
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
  // The plan chain's Claim step: also the only public read of the pointer's
  // latest position. Unknown pointers fail closed here (claimed=false).
  ArenaAction current = arena.Claim(targetId, pointerId);
  if (!current.claimed) {
    return current;
  }
  DownOrigin* origin = FindOrigin(pointerId);
  if (origin == nullptr) {
    // No RecordTapDown seen (contract hermetic cases drive AddPointer
    // directly): seed from the arena's current position and return its
    // claim; displacement is zero so slop cannot trip on this path.
    StoreOrigin(pointerId, current.offsetX, current.offsetY);
    return current;
  }
  const float dx = current.offsetX - origin->x;
  const float dy = current.offsetY - origin->y;
  // Existing slop semantics (runtime/input.h): magnitude at/over slop is a
  // scroll — the tap never fires.
  if (std::hypot(dx, dy) >= ::runtime::touch_detail::kTouchSlopPx) {
    return ArenaAction{-1, 0.0f, 0.0f, false};
  }
  return current;
}

void RecordTapDown(int32_t pointerId, float x, float y) {
  StoreOrigin(pointerId, x, y);
}
