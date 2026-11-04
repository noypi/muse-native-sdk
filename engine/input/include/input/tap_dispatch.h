#pragma once

#include <unordered_map>
#include <unordered_set>

// FR-INPUT-012 / AC-INPUT-024a (specs-input-gestures.md 3.10; FR-M3-013 Notes
// cross-ref) — chain-side winner-to-handle tap bridge (T6).
//
// Canonical route: the gesture chain resolves a tap winner, mints a
// single-use lease for the owning handle, and delivers it with
//   int dispatchTapToOwner(int nodeId, LeaseTicket ticket)
// which presses the owner's handle at most once per tapId. JNI carries only
// the tapId integer identity (never a raw pointer); a ticket reconstructed
// from that integer alone is the same lease. Tickets are value types with no
// heap identity, so there is nothing for a collector to free mid-flight:
// single-use and GC-safe by construction.
//
// Registry ownership (FR-M3-013 Notes): pending leases live ONLY per-button
// (one entry per mounted handle inside each PendingRegistry, created on
// mount, deleted on unmount — never module-global). The process-wide bridge
// state holds idempotency keys only (minted/consumed tapId sets plus the
// nodeId -> mounted-handle routing index), mirroring the JS lane where the
// module-global sets hold keys and each handle owns its pending Map.
//
// Fail-closed (AC-INPUT-024): unknown nodeId, unmounted/removed handle,
// replayed ticket, or forged ticket (never minted) delivers 0 and throws
// nothing through the tap path. Denials consume nothing: a ticket denied for
// the wrong target stays live for the owning handle.
namespace input {

// Opaque single-use lease ticket: integer identity only.
struct LeaseTicket {
  int tapId = -1;
};

// Per-button pending-lease registry. One entry per mounted handle.
class PendingRegistry {
 public:
  PendingRegistry();
  ~PendingRegistry();

  PendingRegistry(const PendingRegistry&) = delete;
  PendingRegistry& operator=(const PendingRegistry&) = delete;

  // Entry created on mount, deleted on unmount (AC-INPUT-024
  // entry-deleted-on-unmount). Unmount spends every pending lease of the
  // handle so no stale ticket can fire a remounted handle.
  void mount(int nodeId);
  void unmount(int nodeId);

  // Mint on winner resolve: records tapId as a live lease of nodeId's
  // pending entry. Mirrors the JS lane (`tap-N` string there, integer here;
  // both recorded verbatim, ingress accepts either spelling).
  LeaseTicket mint(int nodeId, int tapId);

  // Deliveries observed for nodeId by this registry.
  int pressCount(int nodeId) const;

 private:
  friend int dispatchTapToOwner(int nodeId, LeaseTicket ticket);

  std::unordered_map<int, int> pending_;     // tapId -> nodeId (this registry)
  std::unordered_map<int, int> deliveries_;  // nodeId -> deliveries observed
  std::unordered_set<int> mounted_;          // nodeIds mounted by THIS registry
};

// Chain-side dispatch owned by FR-INPUT-012. Returns 0|1 deliveries; the
// component-side press fires at most once per tapId.
int dispatchTapToOwner(int nodeId, LeaseTicket ticket);

}  // namespace input
