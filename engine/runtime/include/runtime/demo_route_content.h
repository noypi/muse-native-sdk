#pragma once

#include <cstddef>
#include <string>

// Native demo route-content provider (engine-eval T1; FR-MUSE-002,
// FR-MUSE-004, RULE-MUSE-006).
//
// Serves byte-identical copies of the `proxy/lib/stub.js` fixtures behind
// the existing `ContentRegistry` tag seam + `RouterBridge` funnel for the
// demo guess path only (no JS engine on device, no dispatch-logic port):
// `FIXTURES.segment` under the `segment` route tag and
// `reason.streams.masked` under the `reason` route tag. The stub file is
// the single source of truth; these bytes are a mirror, never a
// re-implementation (any fixture change is a byte-sync here, zero funnel
// change). No `JsRuntime` handle reaches this TU; no network, no key, no
// upstream dial. Fail-closed throughout; never throws.

namespace runtime {

// Route tags served by this provider. They match the proxy route names in
// `proxy/proxy.config.json` and resolve through `GlobalContentRegistry`
// like every other content tag (exact match; unknown/empty resolves
// nullptr per RULE-RUNTIME-001).
const char* DemoRouteSegmentTag();
const char* DemoRouteReasonTag();

// Plan T1 `Interface:` line (verbatim):
// `RegisterDemoRouteContent(): bool` — `true` iff the demo route tags
// registered first-wins; unknown/empty tag resolves `nullptr`
// (RULE-RUNTIME-001 preserved).
//
// Installs `recordTap` + scene-kind `drawSceneKind` handlers under both
// route tags. Returns true only when both tags were newly registered;
// false when either tag was already present (first wins — the stored
// handlers are untouched) or when registration otherwise fails. A second
// call therefore returns false and changes nothing. Never throws.
bool RegisterDemoRouteContent();

// Byte-mirror accessors. The returned storage is owned by this TU (valid
// for process lifetime); the bytes are byte-identical to the E2
// fingerprinted fixture region (`FIXTURES.segment` JSON + the two
// `reason.streams.masked` chunk strings). Out-of-range chunk index
// returns nullptr (fail-closed, never a default chunk). Never throws.
const char* DemoSegmentJson();
std::size_t DemoSegmentJsonSize();
std::size_t DemoReasonMaskedCount();
const char* DemoReasonMaskedChunk(std::size_t index);

// Fail-closed kind gate for the mirrored route content. True for every
// component the retained and raster paths can present (delegates to the
// existing `IsKnownGalleryComponent` validator); false + a NAMED
// diagnostic (`unknown gallery component kind '<kind>'`, never
// substituted) for anything else, including empty input. `error == nullptr`
// is tolerated (false still returned). Never throws.
bool CheckDemoRouteKind(const std::string& kind, std::string* error);

// Validates the embedded mirrors in place: pinned sizes plus a numeric
// scan rejecting non-finite literals (bad numerics fail, never
// substitute). True with `*error` untouched when the mirrors are intact;
// false + a named diagnostic otherwise. `error == nullptr` is tolerated.
// Never throws.
bool ValidateDemoRouteBytes(std::string* error);

// Number-token scan over arbitrary JSON bytes: every numeric literal must
// parse and be finite (`nan`/`inf`/overflow fail, never substitute).
// `true`/`false`/`null` and string contents are not numbers and are
// skipped. Null/length-zero input fails closed. `error == nullptr` is
// tolerated. Never throws.
bool CheckDemoRouteJsonNumbers(const char* json, std::size_t len,
                               std::string* error);

}  // namespace runtime
