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
  // Re-create the presenter surface+swapchain on the fresh window first,
  // then re-arm the host surface only on success (rollback-on-error), so a
  // null/invalid recreate leaves host AND presenter agreeing on failure.
  // Tree untouched (RULE-RUNTIME-004).
  PresenterError recreated = OnSurfaceRecreated(opaque_window);
  host.surface_live = (recreated.code == PresenterCode::kOk);
  return recreated;
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
    // Roll back the simulate re-arm so host agrees with the presenter
    // no-surface failure; tree untouched.
    host.surface_live = false;
    return destroyed;
  }
  PresenterError recreated = OnSurfaceRecreated(fresh_window);
  host.surface_live = (recreated.code == PresenterCode::kOk);
  return recreated;
}

}  // namespace runtime
