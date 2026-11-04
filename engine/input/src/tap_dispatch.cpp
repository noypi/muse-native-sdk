#include "input/tap_dispatch.h"

#include <mutex>

namespace input {
namespace {

// Process-wide bridge state: idempotency keys + routing index ONLY — never a
// lease registry. Pending leases live inside each PendingRegistry (one Map
// per handle); this state holds the minted/consumed tapId key sets (mirroring
// the JS lane's module-global key sets) plus the nodeId -> mounted-handle
// index dispatch needs to find the owning handle. Single-threaded by
// construction like the rest of engine/input (arena/hit-test path), except
// the JNI lane mounts under the render-thread frame lock while the UP fire
// path mints + dispatches on the UI thread — so every State() touch below
// holds StateMutex (per-registry member maps stay under the caller's handle
// lock, matching the tap-tree mu discipline).
struct BridgeState {
  std::unordered_map<int, PendingRegistry*> owners;  // nodeId -> live handle
  std::unordered_set<int> minted;                    // every tapId ever minted
  std::unordered_set<int> consumed;  // tapIds terminally spent (fired/revoked)
};

BridgeState& State() {
  static BridgeState state;
  return state;
}

std::mutex& StateMutex() {
  static std::mutex mu;
  return mu;
}

}  // namespace

PendingRegistry::PendingRegistry() = default;

PendingRegistry::~PendingRegistry() {
  // Never leave a dangling routing entry: deregister everything this
  // registry mounted so a later dispatch cannot reach a dead handle.
  // Pending leases are spent (consumed), mirroring unmount.
  std::lock_guard<std::mutex> lock(StateMutex());
  for (const int nodeId : mounted_) {
    auto& owners = State().owners;
    const auto owner = owners.find(nodeId);
    if (owner != owners.end() && owner->second == this) {
      owners.erase(owner);
    }
  }
  for (const auto& entry : pending_) {
    State().consumed.insert(entry.first);
  }
}

void PendingRegistry::mount(int nodeId) {
  std::lock_guard<std::mutex> lock(StateMutex());
  mounted_.insert(nodeId);
  State().owners[nodeId] = this;
  if (deliveries_.find(nodeId) == deliveries_.end()) {
    deliveries_[nodeId] = 0;
  }
}

void PendingRegistry::unmount(int nodeId) {
  std::lock_guard<std::mutex> lock(StateMutex());
  if (mounted_.find(nodeId) == mounted_.end()) {
    return;
  }
  mounted_.erase(nodeId);
  auto& state = State();
  const auto owner = state.owners.find(nodeId);
  if (owner != state.owners.end() && owner->second == this) {
    state.owners.erase(owner);
  }
  // Entry deleted on unmount: spend every pending lease of this handle so no
  // stale ticket can fire a remounted handle.
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second == nodeId) {
      state.consumed.insert(it->first);
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

LeaseTicket PendingRegistry::mint(int nodeId, int tapId) {
  // Mount-only-at-lifecycle (AC-INPUT-024 entry-created-on-mount): the JNI
  // fire path mints for the resolved winner without mounting, so a mint for
  // an unmounted target records nothing — no bridge key, no pending entry —
  // and the returned ticket keeps the invalid shape so dispatchTapToOwner
  // denies it fail-closed (forged-shaped, replay-safe, throws nothing).
  std::lock_guard<std::mutex> lock(StateMutex());
  if (mounted_.find(nodeId) == mounted_.end()) {
    return LeaseTicket{-1};
  }
  State().minted.insert(tapId);
  pending_[tapId] = nodeId;
  return LeaseTicket{tapId};
}

int PendingRegistry::pressCount(int nodeId) const {
  std::lock_guard<std::mutex> lock(StateMutex());
  const auto it = deliveries_.find(nodeId);
  return it == deliveries_.end() ? 0 : it->second;
}

int dispatchTapToOwner(int nodeId, LeaseTicket ticket) {
  std::lock_guard<std::mutex> lock(StateMutex());
  auto& state = State();
  // Forged: never minted by any handle.
  if (state.minted.find(ticket.tapId) == state.minted.end()) {
    return 0;
  }
  // Replay: already terminally spent.
  if (state.consumed.find(ticket.tapId) != state.consumed.end()) {
    return 0;
  }
  // Unknown/unmounted target: no live handle owns this node.
  const auto owner = state.owners.find(nodeId);
  if (owner == state.owners.end() || owner->second == nullptr) {
    return 0;
  }
  PendingRegistry* const registry = owner->second;
  // Foreign: this handle holds no live lease with this tapId.
  const auto lease = registry->pending_.find(ticket.tapId);
  if (lease == registry->pending_.end() || lease->second != nodeId) {
    return 0;
  }
  // Consume-before-notify: single-use, exactly-once per tapId.
  registry->pending_.erase(lease);
  state.consumed.insert(ticket.tapId);
  ++registry->deliveries_[nodeId];
  return 1;
}

}  // namespace input
