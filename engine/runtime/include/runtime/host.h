#pragma once

#include <cstddef>

namespace runtime {

// Fake platform surface behind the A2 seam (plan A2): default-constructible,
// in-process only; no JNI, no device, no I/O. The parameter exists so the
// host call site binds to a surface without platform linkage.
struct Surface {
  Surface() = default;
};

// Host lifecycle (FR-RUNTIME-005 / AC-RUNTIME-011). Host owns the surface,
// engine owns the tree (RULE-RUNTIME-004). Single Activity assumption (A1):
// one host per Activity; hosts share no state so two hosts are independent.
//
// H3 (FR-RUNTIME-005 / AC-RUNTIME-013): config-change and background/
// foreground transitions preserve the tree (no re-instantiation, A1) and
// re-create the surface without a dangling reference (RULE-RUNTIME-004).
struct Host {
  bool surface_live = false;
  std::size_t tree_nodes = 0;
  bool backgrounded = false;
};

// Create gives a live surface and a non-empty tree.
inline Host create_host(Surface /*s*/) {
  Host h;
  h.surface_live = true;
  h.tree_nodes = 1;
  return h;
}

// Destroy releases the surface and the tree; double-destroy is a no-op.
inline void destroy_host(Host& h) {
  h.surface_live = false;
  h.tree_nodes = 0;
  h.backgrounded = false;
}

inline bool surface_alive(const Host& h) { return h.surface_live; }

inline std::size_t live_tree_nodes(const Host& h) { return h.tree_nodes; }

// H3: config change re-creates the surface in place; the tree (and its node
// ids) is untouched — no re-instantiation (A1). No-op on a destroyed host
// so a dead host is never resurrected.
inline void simulate_config_change(Host& h) {
  if (!h.surface_live && h.tree_nodes == 0 && !h.backgrounded) {
    return;
  }
  if (h.backgrounded) {
    return;
  }
  h.surface_live = true;
}

// H3: background detaches the surface but keeps the tree; idempotent so a
// double-bg never blanks the tree. No-op on a destroyed host.
inline void to_background(Host& h) {
  if (!h.surface_live && h.tree_nodes == 0) {
    return;
  }
  h.backgrounded = true;
  h.surface_live = false;
}

// H3: foreground re-creates the surface after a background; a foreground
// without a preceding background is a no-op. The tree is never touched.
inline void to_foreground(Host& h) {
  if (!h.backgrounded) {
    return;
  }
  h.backgrounded = false;
  if (h.tree_nodes > 0) {
    h.surface_live = true;
  }
}

// H3: the live tree still holds every pre-transition node.
inline bool tree_intact(const Host& h) { return h.tree_nodes > 0; }

// H3: the surface reference is live (re-created, never dangling).
inline bool surface_valid(const Host& h) { return h.surface_live; }

}  // namespace runtime
