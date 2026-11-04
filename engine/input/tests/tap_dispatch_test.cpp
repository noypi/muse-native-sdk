// TC-INPUT-022 (AC-INPUT-022) + TC-INPUT-024 (AC-INPUT-024) — tap-chain
// winner dispatches to the owning component event handle exactly once;
// unmatched, unmounted, replayed, or forged targets fire nothing
// (FR-INPUT-012). Batch 1 RED pin + T6 (GR-08 T1.1) RED pins.
//
// Hermetic: C++17 + engine/input headers only; layout geometry is stubbed
// in-process (literal rects); no network, no env/config reads, no
// filesystem, no live services, no Android NDK.
// Include path follows the engine/input convention:
//   #include "gesture_arena.h" / #include "hit_test.h" with
//   engine/input/include on the path; NormalizedEvent comes from
//   #include "input/event_model.h".
//
// Settled bridge signature (AC-INPUT-024a, specs-input-gestures.md 3.10;
// implemented in input/tap_dispatch.h, owned by FR-INPUT-012):
//   dispatchTapToOwner(nodeId, leaseTicket) -> press(leaseTicket)
// at most once per tapId. Per-button pending registry (one entry per mounted
// handle, created on mount, deleted on unmount — never module-global);
// opaque single-use { tapId } lease ticket; JNI carries only the tapId
// integer identity (never a raw pointer); ticket lifetime: minted on winner
// resolve, consumed on first delivery attempt, revoked on drag/slop/CANCEL
// or unmount, unreferenceable after consume/revoke. Fail-closed: unknown
// nodeId, unmounted/removed handle, replayed ticket, or forged ticket fires
// nothing (forgery surfaces only as the honest TypeError at the press()
// boundary per AC-M3-036, never as a throw through the tap path).
//
// Batch-1 RED state resolved by T6: the bridge IS implemented —
// input/tap_dispatch.h provides the settled route below (verified by the
// pins in main; HAVE_DISPATCH == 1). No fake dispatch is substituted: a
// passing fake while the Real is missing would be a green-on-divergent-Fake
// invalid suite (GR-12).
//
// C++ BINDING (reconciled at T6 green per AC-INPUT-024a; behavior above is
// normative, spelling is the settled route): input/tap_dispatch.h provides
//   namespace input {
//   struct LeaseTicket { int tapId = -1; };  // opaque single-use ticket
//   class PendingRegistry {                  // per-button scope
//    public:
//     void mount(int nodeId);                 // entry created on mount
//     void unmount(int nodeId);               // entry deleted on unmount
//     LeaseTicket mint(int nodeId, int tapId);// mint on winner resolve
//     int pressCount(int nodeId) const;       // deliveries observed
//   };
//   int dispatchTapToOwner(int nodeId, LeaseTicket ticket);  // 0|1 deliveries
//   }
// The TC-INPUT-024 cases below name behavior from AC-INPUT-024, not API
// beyond the settled signature + the registry lifecycle the AC text states.
#include <cstdint>
#include <cstdio>

#include "gesture_arena.h"
#include "hit_test.h"
#include "input/event_model.h"

#if __has_include("input/tap_dispatch.h")
#include "input/tap_dispatch.h"
#define HAVE_DISPATCH 1
#else
#define HAVE_DISPATCH 0
#endif

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

input::NormalizedEvent make_touch(std::int32_t pointerId, float x, float y) {
  input::NormalizedEvent ev{};
  ev.pointerId = pointerId;
  ev.tool = input::ToolType::Touch;
  ev.sourceClass = input::SourceClass::Touch;
  ev.axes.x = x;
  ev.axes.y = y;
  ev.axes.pressure = 1.0f;
  return ev;
}

HitTestTree make_tree_with_button() {
  HitTestTree tree;
  tree.AddNode(/*id=*/1, /*parentId=*/0, HitRect{0.0f, 0.0f, 1080.0f, 2400.0f});
  tree.AddNode(/*id=*/21, /*parentId=*/1, HitRect{100.0f, 200.0f, 400.0f, 120.0f});
  return tree;
}

}  // namespace

int main() {
  // Arrange (settled surface): a tap inside the button rect resolves a winner.
  HitTestTree tree = make_tree_with_button();
  GestureArena arena;
  const ArenaAction action =
      arena.AddPointer(make_touch(/*pointerId=*/7, /*x=*/150.0f, /*y=*/250.0f), tree);
  check(action.winnerId == 21, "arrange: tap inside button rect resolves winner node 21");

#if HAVE_DISPATCH
  // Normative TC-INPUT-022 pins (Batch 1, unchanged):
  // (a) dispatch_to_handle(action.winnerId, ...) delivers exactly once;
  //     assert deliveries == 1.
  // (b) tap at (900, 2200) (no node) -> deliveries == 0.
  // (c) tap at a removed node id -> deliveries == 0.
  // (d) same tap dispatched twice -> deliveries == 1 in total.
  //
  // (b)-(d) are covered by the TC-INPUT-024 fail-closed block below
  // (unknown nodeId, unmounted target, replayed lease); (a) is pinned here
  // as the dispatch-once assertion against the arranged winner above.
  {
    input::PendingRegistry registry;
    registry.mount(action.winnerId);
    const input::LeaseTicket t = registry.mint(action.winnerId, /*tapId=*/201);
    const int d = input::dispatchTapToOwner(action.winnerId, t);
    check(d == 1 && registry.pressCount(action.winnerId) == 1,
          "TC-INPUT-022: winner dispatch delivers exactly once per tapId");
  }

  // Normative TC-INPUT-024 pins (AC-INPUT-024, T6). Each case is independent
  // (own registry + tapId). Fail-closed: every denial below must deliver 0
  // AND throw nothing through the tap path.
  {
    // (a) dispatch exactly once per tapId.
    input::PendingRegistry registry;
    registry.mount(21);
    const input::LeaseTicket t = registry.mint(21, /*tapId=*/101);
    const int d = input::dispatchTapToOwner(21, t);
    check(d == 1 && registry.pressCount(21) == 1,
          "TC-INPUT-024(a): dispatchTapToOwner delivers exactly once per tapId");
  }
  {
    // (b) replayed lease denied: same ticket dispatched twice -> 1 total.
    input::PendingRegistry registry;
    registry.mount(21);
    const input::LeaseTicket t = registry.mint(21, /*tapId=*/102);
    const int first = input::dispatchTapToOwner(21, t);
    const int replay = input::dispatchTapToOwner(21, t);
    check(first == 1 && replay == 0 && registry.pressCount(21) == 1,
          "TC-INPUT-024(b): replayed lease ticket delivers nothing (total still 1)");
  }
  {
    // (c) forged lease denied: fabricated ticket (never minted) fires nothing.
    input::PendingRegistry registry;
    registry.mount(21);
    const input::LeaseTicket forged{/*tapId=*/103};  // not from mint(): forged
    int d = -1;
    bool threw = false;
    try {
      d = input::dispatchTapToOwner(21, forged);
    } catch (...) {
      threw = true;
    }
    check(!threw && d == 0 && registry.pressCount(21) == 0,
          "TC-INPUT-024(c): forged lease ticket fires nothing and throws nothing");
    // (c2) unknown nodeId silent: valid ticket, no such target.
    const input::LeaseTicket t = registry.mint(21, /*tapId=*/104);
    bool threw2 = false;
    int d2 = -1;
    try {
      d2 = input::dispatchTapToOwner(/*nodeId=*/99, t);
    } catch (...) {
      threw2 = true;
    }
    check(!threw2 && d2 == 0 && registry.pressCount(21) == 0,
          "TC-INPUT-024(c2): unknown nodeId fires nothing and throws nothing");
  }
  {
    // (d) unmounted target silent: entry deleted on unmount.
    input::PendingRegistry registry;
    registry.mount(21);
    const input::LeaseTicket t = registry.mint(21, /*tapId=*/105);
    registry.unmount(21);
    bool threw = false;
    int d = -1;
    try {
      d = input::dispatchTapToOwner(21, t);
    } catch (...) {
      threw = true;
    }
    check(!threw && d == 0,
          "TC-INPUT-024(d): unmounted target fires nothing and throws nothing");
  }
  {
    // (e) JNI tapId-only identity: a ticket reconstructed from the tapId
    // integer alone (value copy, as carried across JNI — never a pointer)
    // is the same lease and delivers; the original is then consumed.
    input::PendingRegistry registry;
    registry.mount(21);
    const input::LeaseTicket t = registry.mint(21, /*tapId=*/106);
    const input::LeaseTicket acrossJni{t.tapId};  // integer identity only
    const int d = input::dispatchTapToOwner(21, acrossJni);
    const int again = input::dispatchTapToOwner(21, t);
    check(d == 1 && again == 0 && registry.pressCount(21) == 1,
          "TC-INPUT-024(e): tapId-integer identity delivers once, then consumed");
  }
  {
    // (f) mount-lifecycle: mounted target fires, then unmounted target is
    // silent (entry deleted on unmount; AC-INPUT-024 entry-deleted-on-unmount).
    input::PendingRegistry registry;
    registry.mount(21);
    const input::LeaseTicket t = registry.mint(21, /*tapId=*/107);
    const int d = input::dispatchTapToOwner(21, t);
    check(d == 1 && registry.pressCount(21) == 1,
          "TC-INPUT-024(f): mounted target fires on tap");
    registry.unmount(21);
    const input::LeaseTicket t2 = registry.mint(21, /*tapId=*/108);
    bool threw = false;
    int d2 = -1;
    try {
      d2 = input::dispatchTapToOwner(21, t2);
    } catch (...) {
      threw = true;
    }
    check(!threw && d2 == 0 && registry.pressCount(21) == 1,
          "TC-INPUT-024(f): unmounted target stays silent after unmount");
  }
  {
    // (g) scrim lifecycle: the full-bleed layering scrim (node 20) follows
    // the same mount -> fires / unmount -> silent lifecycle as a button.
    input::PendingRegistry registry;
    registry.mount(20);
    const input::LeaseTicket t = registry.mint(20, /*tapId=*/109);
    const int d = input::dispatchTapToOwner(20, t);
    check(d == 1 && registry.pressCount(20) == 1,
          "TC-INPUT-024(g): mounted scrim fires on tap");
    registry.unmount(20);
    const input::LeaseTicket t2 = registry.mint(20, /*tapId=*/110);
    bool threw = false;
    int d2 = -1;
    try {
      d2 = input::dispatchTapToOwner(20, t2);
    } catch (...) {
      threw = true;
    }
    check(!threw && d2 == 0 && registry.pressCount(20) == 1,
          "TC-INPUT-024(g): unmounted scrim stays silent after unmount");
  }
  {
    // (h) fire-path-never-mounts: mint + dispatch with no prior mount fires
    // nothing. Minting records a pending lease but creates no ownability;
    // only the component-mount lifecycle mounts, never the fire path.
    input::PendingRegistry registry;
    const input::LeaseTicket t = registry.mint(/*nodeId=*/42, /*tapId=*/111);
    bool threw = false;
    int d = -1;
    try {
      d = input::dispatchTapToOwner(/*nodeId=*/42, t);
    } catch (...) {
      threw = true;
    }
    check(!threw && d == 0 && registry.pressCount(42) == 0,
          "TC-INPUT-024(h): mint+dispatch with no prior mount fires nothing");
  }
#else
  check(false,
        "TC-INPUT-022 RED: no winner-to-handle dispatch route "
        "(input/tap_dispatch.h missing; T6 HALTED pending specs-creator)");
  check(false,
        "TC-INPUT-024 RED: no dispatchTapToOwner(nodeId, leaseTicket) route "
        "(input/tap_dispatch.h missing; bridge unimplemented)");
#endif

  if (g_failures == 0) {
    std::printf("TC-INPUT-022+024 PASS\n");
    return 0;
  }
  std::printf("TC-INPUT-022+024 FAIL (%d failing)\n", g_failures);
  return 1;
}
