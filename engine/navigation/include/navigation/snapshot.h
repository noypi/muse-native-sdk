#pragma once

#include <cstddef>
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
  bool truncated = false;
};

// Provider callables keyed by route id. Empty return = destination opted out
// (no content emitted for that route). std::function form so both lambdas
// and function pointers bind at call sites.
using ScrollProvider = std::function<std::string(const std::string& routeId)>;
using FocusProvider = std::function<std::string(const std::string& routeId)>;
// Telemetry sink for truncation events. std::function form so both lambdas
// and function pointers bind at call sites (same pattern as T2 providers).
using TelemetrySink =
    std::function<void(const std::string& event, const std::string& detail)>;

// Injectable sibling hooks for the restore commit gate (T5). ValidateFn is
// the FR-NAV-006 destination validator (false = reject); GuardFn is the
// FR-NAV-003 guard re-run (false = deny). std::function form so lambdas and
// function pointers bind at call sites (same pattern as T2 providers). A
// null/empty hook allows every route, preserving pre-gate behavior.
using ValidateFn = std::function<bool(const std::string& routeId)>;
using GuardFn = std::function<bool(const std::string& routeId)>;

enum class RestoreError {
  NONE,
  VALIDATION_REJECTED,
  GUARD_DENIED,
  UNKNOWN_ROUTE_HOME,
  CORRUPT,
};

struct RestoreResult {
  bool committed = false;
  RestoreError error = RestoreError::CORRUPT;
};

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

// Oversize truncation by priority with telemetry (AC-NAV-027). Drops content
// sections first (focus, then scroll), then deepest stack entries, never the
// home root. The structural prefix is never cut: content is stripped at
// `;content=` and stack entries are removed by rebuilding the canonical
// `NAV1;v=1;...` envelope, so truncated output still restores via
// `NavGraph::restore`. Emits one `nav.snapshot.truncated` event per dropped
// section; a null/empty sink is ignored. Returns {ok=true, blob<=maxBytes,
// truncated=true} on success, or {ok=false, error=CORRUPT} when the blob is
// malformed/empty or even the home root exceeds budget. Never throws.
SnapshotResult truncateSnapshot(const std::string& blob, size_t maxBytes,
                                TelemetrySink sink) noexcept;

// Restore commit gate (AC-NAV-003/006/014, NFR-NAV-002). Decodes `blob` via
// T4 `decodeState`, re-runs the sibling FR-NAV-006 validator per route, then
// re-runs the FR-NAV-003 guards per route, and commits via `NavGraph::restore`
// as one transaction. Validation/guard failure commits nothing
// (VALIDATION_REJECTED/GUARD_DENIED); an unknown route commits home
// (UNKNOWN_ROUTE_HOME), never a crash; corrupt/empty input commits nothing
// (CORRUPT). Null/empty hooks allow every route. Never throws.
RestoreResult commitRestore(const std::string& blob, ValidateFn validate,
                            GuardFn guard) noexcept;
