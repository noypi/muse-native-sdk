// T3+T4 contract tests — FR-PVIEWS-009 / AC-PVIEWS-009 / AC-PVIEWS-010 /
// TC-PVIEWS-009 / TC-PVIEWS-010.
// Bind ONLY to the plan T3/T4 `Interface:` lines; never to implementation.
// Hermetic: C++17 + pviews/embed.h (exists, EngineHandle) + pviews/interop.h
// (to-be-created) only; integer-fake EngineHandle/ComposeNode/ComposeScope/
// HostOwners values, no Android View/Compose classes, no device, no network,
// no env/config reads, no filesystem, no live services. Errors are return
// values, never thrown: every error-path case also proves no exception
// escapes (try/catch around the call).
//
// RED state (Batch 2, pre-coder): pviews/interop.h and src/interop.cpp do not
// exist yet, so this file does NOT compile. That compile failure is the
// expected red for every case below.
//
// ASSUMED header shape (see unit-tests/T3.md and T4.md; coder must satisfy
// it; helpers beyond the `Interface:` lines are marked ASSUMPTION there):
//   struct ComposeNode { uint64_t value; };   // 0 == invalid
//   struct ComposeScope { uint64_t value; };  // 0 == invalid
//   struct HostOwners {                       // 0 field == owner missing
//     uint64_t lifecycleOwner;
//     uint64_t savedStateRegistryOwner;
//     uint64_t viewModelStoreOwner;
//   };
//   struct ComposeSlot { ComposeNode node; HostOwners owners;
//                        uint64_t dispatchCount = 0; };
//   enum class DisposeStrategy {
//     DisposeOnLeaveComposition = 0,
//     DisposeOnViewTreeLifecycleDestroyed = 1
//   };
//   struct SurfaceSlot { uint64_t id = 0; bool disposed = false; };
//   enum class InteropErrorCode {
//     NONE = 0, INTEROP_NO_OWNER = 1, INTEROP_DOUBLE_ATTACH = 2,
//     INTEROP_LEAKED_SURFACE = 3
//   };
//   struct ComposeSlotResult { bool ok; ComposeSlot slot;
//                              InteropErrorCode error; };
//   struct SurfaceSlotResult { bool ok; SurfaceSlot slot;
//                              InteropErrorCode error; };
//   ComposeSlotResult attachComposeNode(EngineHandle engine, ComposeNode node,
//                                       HostOwners owners) noexcept;
//   void dispatchLifecycleEvent(ComposeSlot& slot, int event) noexcept;
//       // ASSUMPTION: test-only dispatch hook for exactly-once proof
//   SurfaceSlotResult attachEngineSurface(ComposeScope compose,
//                                         EngineHandle engine,
//                                         DisposeStrategy strategy) noexcept;
//   void detachOnDispose(SurfaceSlot& slot) noexcept;
//   bool surfaceIsDisposed(const SurfaceSlot& slot) noexcept;
//       // ASSUMPTION: observable disposal probe
//   InteropErrorCode auditSurfaceLeak(const SurfaceSlot& slot) noexcept;
//       // ASSUMPTION: NONE when disposed, INTEROP_LEAKED_SURFACE when live;
//       // reports the plan's "dispose skipped" error as a return value.
#include <cstdint>
#include <cstdio>

#include "pviews/embed.h"
#include "pviews/interop.h"

static HostOwners kFullOwners() {
  return HostOwners{101, 102, 103};
}

// ---- T3: Compose-in-engine host owners ----

// Case: valid engine + node + all three host owners -> ok + slot echoes
// the node and the owners the node must observe.
static bool check_attach_with_full_owners_ok() {
  HostOwners owners = kFullOwners();
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{1}, ComposeNode{10}, owners);
    return r.ok && r.slot.node.value == 10 &&
           r.slot.owners.lifecycleOwner == 101 &&
           r.slot.owners.savedStateRegistryOwner == 102 &&
           r.slot.owners.viewModelStoreOwner == 103 &&
           r.error == InteropErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: missing LifecycleOwner -> INTEROP_NO_OWNER, never throws.
static bool check_missing_lifecycle_owner_denied() {
  HostOwners owners{0, 102, 103};
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{1}, ComposeNode{11}, owners);
    return !r.ok && r.error == InteropErrorCode::INTEROP_NO_OWNER;
  } catch (...) {
    return false;  // errors are values, never exceptions
  }
}

// Case: missing SavedStateRegistryOwner -> INTEROP_NO_OWNER, never throws.
static bool check_missing_saved_state_owner_denied() {
  HostOwners owners{101, 0, 103};
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{1}, ComposeNode{12}, owners);
    return !r.ok && r.error == InteropErrorCode::INTEROP_NO_OWNER;
  } catch (...) {
    return false;
  }
}

// Case: missing ViewModelStoreOwner -> INTEROP_NO_OWNER, never throws.
static bool check_missing_viewmodel_owner_denied() {
  HostOwners owners{101, 102, 0};
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{1}, ComposeNode{13}, owners);
    return !r.ok && r.error == InteropErrorCode::INTEROP_NO_OWNER;
  } catch (...) {
    return false;
  }
}

// Case: no owners at all -> INTEROP_NO_OWNER, never throws.
static bool check_no_owners_denied() {
  HostOwners owners{0, 0, 0};
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{1}, ComposeNode{14}, owners);
    return !r.ok && r.error == InteropErrorCode::INTEROP_NO_OWNER;
  } catch (...) {
    return false;
  }
}

// Case: attaching the same node twice -> INTEROP_DOUBLE_ATTACH on the
// second attach, never throws.
static bool check_double_attach_same_node_denied() {
  try {
    const ComposeSlotResult first =
        attachComposeNode(EngineHandle{2}, ComposeNode{20}, kFullOwners());
    if (!first.ok) return false;
    const ComposeSlotResult second =
        attachComposeNode(EngineHandle{2}, ComposeNode{20}, kFullOwners());
    return !second.ok &&
           second.error == InteropErrorCode::INTEROP_DOUBLE_ATTACH;
  } catch (...) {
    return false;
  }
}

// Case: one lifecycle event -> exactly one dispatch (no duplication,
// no polling gap). ASSUMPTION: dispatchLifecycleEvent test hook.
static bool check_single_event_single_dispatch() {
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{3}, ComposeNode{30}, kFullOwners());
    if (!r.ok) return false;
    ComposeSlot slot = r.slot;
    dispatchLifecycleEvent(slot, 1);
    return slot.dispatchCount == 1;
  } catch (...) {
    return false;
  }
}

// Case: two lifecycle events -> dispatch count 2 (bridge stays 1:1,
// no coalescing, no fan-out). ASSUMPTION: dispatchLifecycleEvent hook.
static bool check_two_events_two_dispatches() {
  try {
    const ComposeSlotResult r =
        attachComposeNode(EngineHandle{3}, ComposeNode{31}, kFullOwners());
    if (!r.ok) return false;
    ComposeSlot slot = r.slot;
    dispatchLifecycleEvent(slot, 1);
    dispatchLifecycleEvent(slot, 2);
    return slot.dispatchCount == 2;
  } catch (...) {
    return false;
  }
}

// ---- T4: Engine-in-Compose disposal ----

// Case: attach surface under DisposeOnLeaveComposition -> ok + live slot.
static bool check_attach_surface_ok() {
  try {
    const SurfaceSlotResult r = attachEngineSurface(
        ComposeScope{50}, EngineHandle{5},
        DisposeStrategy::DisposeOnLeaveComposition);
    return r.ok && r.slot.id != 0 && !surfaceIsDisposed(r.slot) &&
           r.error == InteropErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: leaving composition disposes the native surface synchronously.
static bool check_detach_on_dispose_releases() {
  try {
    const SurfaceSlotResult r = attachEngineSurface(
        ComposeScope{51}, EngineHandle{5},
        DisposeStrategy::DisposeOnLeaveComposition);
    if (!r.ok) return false;
    SurfaceSlot slot = r.slot;
    detachOnDispose(slot);
    return surfaceIsDisposed(slot) &&
           auditSurfaceLeak(slot) == InteropErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: dispose skipped (slot leaves composition without detach) ->
// INTEROP_LEAKED_SURFACE reported as a return value, never thrown.
static bool check_skipped_dispose_reports_leaked_surface() {
  try {
    const SurfaceSlotResult r = attachEngineSurface(
        ComposeScope{52}, EngineHandle{5},
        DisposeStrategy::DisposeOnLeaveComposition);
    if (!r.ok) return false;
    const SurfaceSlot slot = r.slot;  // deliberately never detached
    return !surfaceIsDisposed(slot) &&
           auditSurfaceLeak(slot) == InteropErrorCode::INTEROP_LEAKED_SURFACE;
  } catch (...) {
    return false;
  }
}

// Case: DisposeOnViewTreeLifecycleDestroyed strategy attaches and
// disposes cleanly too (pinned versioned semantics).
static bool check_destroyed_strategy_disposes() {
  try {
    const SurfaceSlotResult r = attachEngineSurface(
        ComposeScope{53}, EngineHandle{6},
        DisposeStrategy::DisposeOnViewTreeLifecycleDestroyed);
    if (!r.ok) return false;
    SurfaceSlot slot = r.slot;
    detachOnDispose(slot);
    return surfaceIsDisposed(slot) &&
           auditSurfaceLeak(slot) == InteropErrorCode::NONE;
  } catch (...) {
    return false;
  }
}

// Case: two surfaces are independent — disposing one leaves the other
// live (no leaked-view cross-talk, no over-release).
static bool check_surfaces_independent() {
  try {
    const SurfaceSlotResult a = attachEngineSurface(
        ComposeScope{54}, EngineHandle{7},
        DisposeStrategy::DisposeOnLeaveComposition);
    const SurfaceSlotResult b = attachEngineSurface(
        ComposeScope{55}, EngineHandle{7},
        DisposeStrategy::DisposeOnLeaveComposition);
    if (!a.ok || !b.ok) return false;
    if (a.slot.id == b.slot.id) return false;
    SurfaceSlot slotA = a.slot;
    const SurfaceSlot slotB = b.slot;
    detachOnDispose(slotA);
    return surfaceIsDisposed(slotA) && !surfaceIsDisposed(slotB) &&
           auditSurfaceLeak(slotB) == InteropErrorCode::INTEROP_LEAKED_SURFACE;
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_attach_with_full_owners_ok);
  RUN_CASE(check_missing_lifecycle_owner_denied);
  RUN_CASE(check_missing_saved_state_owner_denied);
  RUN_CASE(check_missing_viewmodel_owner_denied);
  RUN_CASE(check_no_owners_denied);
  RUN_CASE(check_double_attach_same_node_denied);
  RUN_CASE(check_single_event_single_dispatch);
  RUN_CASE(check_two_events_two_dispatches);
  RUN_CASE(check_attach_surface_ok);
  RUN_CASE(check_detach_on_dispose_releases);
  RUN_CASE(check_skipped_dispose_reports_leaked_surface);
  RUN_CASE(check_destroyed_strategy_disposes);
  RUN_CASE(check_surfaces_independent);
  if (failures > 0) {
    std::printf("compose_interop_test: %d failing\n", failures);
    return 1;
  }
  std::printf("compose_interop_test: all passing\n");
  return 0;
}
