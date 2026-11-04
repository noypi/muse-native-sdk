#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

// Guess-round state machine (engine-eval T2; AC-SAMPLE-033, RULE-MUSE-007).
//
// Plan T2 `Interface:` line (verbatim):
// `PlayGuessRound(photoId, guessPoint): { maskId; reasonChunks[];
// shelfRenderMode: 'by_object' }` — throws/returns named fail-closed errors
// for unknown route, bad polygon, empty reason stream (mirrors
// `guessMask.ts` error names, not its JS).
//
// One-shot and stateless: each call resolves the mirrored segment bytes
// (mask-1 = matches[0].mask truth, mask-2 = suggestions[0] alternative),
// picks the hidden mask deterministically as
// `masks[photoId.size() % masks.size()]` (never the visitor's guess point —
// the point is validated finite and then only routes, exactly like the TS
// `playGuessRound`), attaches the mirrored masked reason chunks and the
// `by_object` shelf entry, emits the T4 `muse_guess` marker triple in
// order, and returns. There is no retained round state, so "reveal
// consumes round state so stale state cannot leak" holds by construction.
// Polygons are parsed out of the mirrored bytes (not copied literals), so
// an upstream byte-sync changes output with zero funnel change (swap
// litmus preserved structurally). Unknown route, bad polygon, and empty
// reason stream fail closed with the `guessMask.ts` names; `out` is
// untouched on failure and `*error` is untouched on success.
// `out == nullptr` fails closed. Never throws.
struct GuessPoint {
  double x = 0.0;
  double y = 0.0;
};

struct GuessPolygonPoint {
  double x = 0.0;
  double y = 0.0;
};

struct GuessRoundResult {
  std::string maskId;                      // "mask-1" or "mask-2"
  std::string maskLabel;                   // routed match name
  std::vector<GuessPolygonPoint> polygon;  // hidden mask polygon
  std::vector<std::string> reasonChunks;   // mirrored masked stream
  std::string shelfRenderMode;             // always "by_object"
};

bool PlayGuessRound(const std::string& photoId, GuessPoint point,
                    GuessRoundResult* out, std::string* error);

// FNV-1a (32-bit) hex of arbitrary bytes — the `polyHash=<h>` leg of the
// T4 marker triple, hashed over the hidden mask's raw polygon bytes so the
// marker proves the content followed the route (not a repaint). Pure and
// deterministic; nullptr/empty input yields "00000000". Never throws.
std::string GuessPolygonHashHex(const char* bytes, std::size_t len);

}  // namespace runtime
