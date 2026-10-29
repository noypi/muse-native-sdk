// H1 production unit: host lifecycle (FR-RUNTIME-005 / AC-RUNTIME-011).
// The implementation lives inline in runtime/host.h so the lifecycle stays
// usable from the header-only seam without extra linkage; this TU keeps the
// conventional src/ unit compiled and checked.
//
// FR-RUNTIME-009 T4 Detail: host lifecycle hooks driving presenter
// teardown/recreate (AC-011/013 baseline extension). Each HostOn* entry
// drives the host transition first (tree handle untouched per
// RULE-RUNTIME-004) and then the matching presenter step, so device
// config-change / background / foreground cannot leave the swapchain in a
// stale state. On-device callers must route lifecycle events through these
// hooks rather than calling the presenter directly.
#include "runtime/host.h"

#include "runtime/presenter.h"

namespace runtime {

PresenterError HostOnSurfaceDestroyed(Host& host) {
  // Surface-only teardown: the tree handle is untouched (RULE-RUNTIME-004).
  // destroy_host() is NOT used here — it releases the tree for full host
  // teardown, while a surface destroy must preserve it for recreate.
  host.surface_live = false;
  return OnSurfaceDestroyed();
}

PresenterError HostOnSurfaceRecreated(Host& host, void* opaque_window) {
  // Re-arm the host surface in place (tree untouched), then re-create the
  // presenter surface+swapchain on the fresh window.
  host.surface_live = true;
  return OnSurfaceRecreated(opaque_window);
}

PresenterError HostOnBackground(Host& host) {
  to_background(host);
  return OnBackground();
}

PresenterError HostOnForeground(Host& host) {
  to_foreground(host);
  return OnForeground();
}

PresenterError HostOnConfigChange(Host& host, void* fresh_window) {
  simulate_config_change(host);
  PresenterError destroyed = OnSurfaceDestroyed();
  if (destroyed.code != PresenterCode::kOk) {
    return destroyed;
  }
  return OnSurfaceRecreated(fresh_window);
}

}  // namespace runtime
