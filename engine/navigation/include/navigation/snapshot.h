#pragma once

#include <functional>
#include <string>

#include "navigation/graph.h"

enum class SnapshotError {
  NONE,
  CORRUPT,
};

struct SnapshotResult {
  bool ok = false;
  std::string blob;
  SnapshotError error = SnapshotError::CORRUPT;
};

// Provider callables keyed by route id. Empty return = destination opted out
// (no content emitted for that route). std::function form so both lambdas
// and function pointers bind at call sites.
using ScrollProvider = std::function<std::string(const std::string& routeId)>;
using FocusProvider = std::function<std::string(const std::string& routeId)>;

// Captures the full nested/tab/modal snapshot of `graph` into a versioned
// blob. The blob is the `NavGraph::serialize()` canonical envelope
// (`NAV1;v=1;...`); future content sections (T2 scroll/focus) are appended
// after a `;content=` marker so the structural prefix stays parseable by
// `NavGraph::restore`. T1 carries no content, so the blob is the bare
// canonical form. Never throws: failures return {ok=false, empty blob,
// CORRUPT}.
SnapshotResult captureSnapshot(const NavGraph& graph) noexcept;

// Opt-in per-destination scroll + focus capture (AC-NAV-026). Structural
// blob is identical to captureSnapshot(); when at least one destination
// returns non-empty content, entries are appended after a `;content=`
// section so the structural prefix stays parseable by NavGraph::restore
// (strip at `;content=`). Fully opted-out captures return the bare blob so
// it restores directly. Error shape matches T1 (NONE/CORRUPT); TRUNCATED is
// never produced here. Never throws.
SnapshotResult captureSnapshotWithContent(const NavGraph& graph,
                                          ScrollProvider scroll,
                                          FocusProvider focus) noexcept;
