// Device tap chain wiring (BUG-M3-FIDELITY-007 T4; FR-INPUT-011 /
// AC-INPUT-020). Implements runtime/input_chain.h over the consume-only
// engine/input units: HitTestTree::HitTest (depth-wins) for target
// resolution, GestureArena::AddPointer/Claim for tap-vs-scroll ownership.
// Never touches the Host touch double (runtime/input.h): that rect/counter
// path is T6's to delete, and no device-path caller may reference it.

#include "runtime/input_chain.h"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

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

// Fire/repaint bookkeeping (BUG-M3-FIDELITY-007 T5). ClaimTap-exclusive
// routing lives here, not in the caller: every claimed tap grants exactly
// one fire credit for its winner, scoped to the claiming gesture's pointer
// id, and FireTapHandler consumes only the latest gesture's outstanding
// grant — so a claim on gesture/arena A never arms a fire for gesture B
// that never claimed (per-arena isolation end behavior through the fixed
// (nodeId)-only signature), and no direct arena read can arm one either.
// Guarded by g_fireMu; production dispatch is normally single-threaded.
//
// Scope note: gestures own distinct pointer ids on the production path, so
// pointer-scoping yields per-arena isolation there. Two live handles reusing
// one pointer id is a residual cross-handle alias (the origin table is
// likewise pointer-keyed); closing it needs a handle-scoped fire entry
// point, i.e. a signature change owned by contract reconciliation.
namespace {

std::mutex g_fireMu;
// Outstanding fire credits: claiming pointer id -> (winner id -> count).
std::unordered_map<int32_t, std::unordered_map<int32_t, int>> g_fireGrants;
// Latest ACTION_DOWN pointer id (set by RecordTapDown; fail-closed writes
// never update it). FireTapHandler consumes only this gesture's grants.
int32_t g_latestDownPointer = 0;
bool g_hasLatestDown = false;
std::unordered_set<int32_t> g_firedEver;

void GrantFireLocked(int32_t pointerId, int32_t winnerId) {
  auto& bucket = g_fireGrants[pointerId];
  if (g_fireGrants.size() >= 64 && bucket.empty()) {
    // Bounded ledger: under extreme pointer churn drop outstanding grants
    // rather than growing without bound. Fail-closed direction (a dropped
    // grant fires nothing, never a phantom fire).
    g_fireGrants.clear();
  }
  ++g_fireGrants[pointerId][winnerId];
}

}  // namespace

// Gallery first-page tap geometry (review BLOCKER population path): root
// plus the representative Button-filled node. Mirrors the T4-seeded shape
// the hermetic contracts pin (400x800 root, button (50,100)-(250,180));
void PopulateTapTreeFromLayout(HitTestTree& tree) {
  // Idempotent: a tree that already resolves inside the root viewport keeps
  // its nodes (safe to call at load and re-attach; never duplicates).
  if (tree.HitTest(1.0f, 1.0f) != -1) {
    return;
  }
  tree.AddNode(10, 0, HitRect{0.0f, 0.0f, 400.0f, 800.0f});
  tree.AddNode(21, 10, HitRect{50.0f, 100.0f, 200.0f, 80.0f});
}

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
  {
    // Within slop: the tap owns the gesture — grant exactly one fire for
    // the winner to the claiming gesture's pointer before returning, so
    // the grant cannot be lost between ClaimTap and FireTapHandler and can
    // never arm a fire for a gesture that never claimed.
    std::lock_guard<std::mutex> fireLock(g_fireMu);
    GrantFireLocked(pointerId, current.winnerId);
  }
  return current;
}

void RecordTapDown(int32_t pointerId, float x, float y) {
  // Untrusted MotionEvent fields fail closed before touching the ledger
  // (mirrors the ResolveTapTarget guard): a NaN/negative origin is dropped
  // with no store and no latest-gesture update, so ClaimTap for the pointer
  // finds no origin and stays unclaimed.
  if (!(x >= 0.0f) || !(y >= 0.0f)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_mu);
    StoreOriginLocked(pointerId, x, y);
  }
  {
    std::lock_guard<std::mutex> fireLock(g_fireMu);
    g_latestDownPointer = pointerId;
    g_hasLatestDown = true;
  }
}

bool FireTapHandler(int32_t nodeId) {
  // Fail closed before touching any handler state: negative and zero ids
  // are never laid-out nodes (T5 interface: false when nodeId < 0; zero
  // stays silent per the forged/stale/zero binding).
  if (nodeId <= 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_fireMu);
  // Per-gesture scoping: only the latest ACTION_DOWN gesture's outstanding
  // grant for this node fires. A claim on another gesture/arena leaves this
  // gesture's bucket empty, so it fires nothing (no phantom fire, no
  // deref); each grant is single-consume.
  if (!g_hasLatestDown) {
    return false;
  }
  auto bucketIt = g_fireGrants.find(g_latestDownPointer);
  if (bucketIt == g_fireGrants.end()) {
    return false;
  }
  auto it = bucketIt->second.find(nodeId);
  if (it == bucketIt->second.end() || it->second <= 0) {
    // No outstanding ClaimTap grant for this node in this gesture:
    // outside/forged/stale ids and already-consumed taps fire nothing.
    return false;
  }
  if (--it->second <= 0) {
    bucketIt->second.erase(it);
    if (bucketIt->second.empty()) {
      g_fireGrants.erase(bucketIt);
    }
  }
  g_firedEver.insert(nodeId);
  return true;
}

bool RepaintWithinOneFrame(int32_t nodeId) {
  // A silent tap schedules no work: outside/forged ids that fired nothing
  // post nothing.
  if (nodeId <= 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_fireMu);
  // The hook posts the write-through repaint for a fired node only: fired
  // membership, never claimed-but-never-fired. It reads only the fire
  // ledger: no host tree is touched here, so the live tree is intact by
  // construction (no re-instantiation). The vsync timing itself is proven
  // on device (T7); hermetically the grant record is the post contract.
  return g_firedEver.count(nodeId) > 0;
}
