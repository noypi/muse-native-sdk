#include "pviews/interop.h"

#include <mutex>
#include <unordered_set>

#include <atomic>

namespace {

// Nodes already attached; a second attach of the same node value is denied.
std::mutex g_mutex;
std::unordered_set<std::uint64_t> g_attached_nodes;

// Monotonic surface ids; disposal state lives in each SurfaceSlot value so
// surfaces stay independent (no cross-talk, no over-release).
std::atomic<std::uint64_t> g_next_surface_id{1};

}  // namespace

ComposeSlotResult attachComposeNode(EngineHandle engine, ComposeNode node,
                                    HostOwners owners) noexcept {
  if (engine.value == 0 || node.value == 0 ||
      owners.lifecycleOwner == 0 ||
      owners.savedStateRegistryOwner == 0 ||
      owners.viewModelStoreOwner == 0) {
    return ComposeSlotResult{false, ComposeSlot{},
                             InteropErrorCode::INTEROP_NO_OWNER};
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_attached_nodes.count(node.value) != 0) {
    return ComposeSlotResult{false, ComposeSlot{},
                             InteropErrorCode::INTEROP_DOUBLE_ATTACH};
  }
  g_attached_nodes.insert(node.value);
  ComposeSlot slot;
  slot.node = node;
  slot.owners = owners;
  slot.dispatchCount = 0;
  return ComposeSlotResult{true, slot, InteropErrorCode::NONE};
}

void dispatchLifecycleEvent(ComposeSlot& slot, int /*event*/) noexcept {
  slot.dispatchCount += 1;
}

// ---- T4: Engine-in-Compose disposal (AC-PVIEWS-010) ----
// Both DisposeStrategy values attach a live surface; the caller releases it
// synchronously via detachOnDispose (Dispose-on-leave-composition). A slot
// that is still live when audited reports INTEROP_LEAKED_SURFACE as a return
// value, never by throwing. Double-detach is safe (idempotent).

SurfaceSlotResult attachEngineSurface(ComposeScope compose,
                                      EngineHandle engine,
                                      DisposeStrategy /*strategy*/) noexcept {
  if (compose.value == 0 || engine.value == 0) {
    return SurfaceSlotResult{false, SurfaceSlot{},
                             InteropErrorCode::NONE};
  }
  SurfaceSlot slot;
  slot.id = g_next_surface_id.fetch_add(1);
  slot.disposed = false;
  return SurfaceSlotResult{true, slot, InteropErrorCode::NONE};
}

void detachOnDispose(SurfaceSlot& slot) noexcept {
  slot.disposed = true;
}

bool surfaceIsDisposed(const SurfaceSlot& slot) noexcept {
  return slot.disposed;
}

InteropErrorCode auditSurfaceLeak(const SurfaceSlot& slot) noexcept {
  if (slot.disposed) return InteropErrorCode::NONE;
  return InteropErrorCode::INTEROP_LEAKED_SURFACE;
}
