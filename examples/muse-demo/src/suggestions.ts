// T3 (BUG-SAMPLE-008, AC-SAMPLE-023/026) — suggestion pipeline for the
// muse-demo photo annotator.
// Pure logic: `segment`-route binding, settle-fired auto-suggestions (768px,
// memoized by photoId+point), and promote-reuses-polygon. Rendering lives in
// `./annotator.tsx`; shelving lives in `./listShelf.tsx`.
//
// Single source of truth: the stub proxy `segment` route
// (proxy/lib/stub.js FIXTURES.segment, reached live through the transport
// below; edge proxy in production). This module holds NO local segment
// geometry — the mask and suggestions are parsed from the route response, so
// swapping the endpoint changes output with no component-code change
// (AC-SAMPLE-026). The injectable `setSegmentRoute` seam is the swap point:
// the composition root (host/dev entry) injects the edge fetcher while CI and
// manual runs keep the stub default.

// Shared geometric polygon type the demo binds (A-T5-1): a closed ring of
// [x, y] points in the 768px suggestion viewport.
export type Polygon = number[][];

// RULE-MUSE-007 rejection shape (never raw text): the code names the class,
// `retry` re-fires the same call with the same arguments.
export type AnnotateErrorCode = 'auth' | 'upstream' | 'timeout' | 'schema';
export type AnnotateResult = { mask: Polygon; suggestions: Polygon[] };
export type AnnotateError = {
  code: AnnotateErrorCode;
  retry: () => Promise<AnnotateResult>;
};

// Width of the suggestion viewport the stub fixtures are authored against.
export const SUGGESTION_VIEWPORT_PX = 768;
// Suggestions fire once the tap settles (no further taps within this window).
export const SETTLE_MS = 150;
// Maximum auto-suggestions per photo (A-T5-2: up to 3; fewer when the stub
// returns fewer; empty is valid when no suggestions fire).
export const MAX_SUGGESTIONS = 3;

type TapPoint = { x: number; y: number };

// --- `segment` route transport -----------------------------------------------
// Request/response shapes follow the stub proxy contract (proxy/lib/stub.js
// `handle({ route: 'segment', photoId, point })` → `{ status, body }`, pinned
// by the T2 segment-contract suite).
export type SegmentRouteRequest = {
  route: 'segment';
  photoId: string;
  point: TapPoint;
  turnId?: string;
};
export type SegmentRouteFn = (req: SegmentRouteRequest) => {
  status: number;
  body: unknown;
};

// Default transport: the stub proxy handle (the T2-pinned RealAdapter).
// Required directly — per the BUG-SAMPLE-008 report, demo sources must reach
// the proxy rather than re-implement its fixtures.
const { handle: stubRouteHandle } = require('../../../proxy/lib/stub.js') as {
  handle: (req: Record<string, unknown>) => { status: number; body: unknown };
};

// Generic provider availability gate (packages/intelligence/lib/routes.js —
// merged-lane file, read-only, never reimplemented here): verifies the Muse
// route descriptors before the first `segment` dispatch, mirroring the
// voice.js T4 pipeline seam. Descriptors are the real on-disk proxy route
// table (proxy/proxy.config.json); the gate itself never calls upstream.
const { assertMuseRoutes } = require('../../../packages/intelligence/lib/routes.js') as {
  assertMuseRoutes: (
    fetchImpl: (name: string) => Promise<unknown> | unknown,
    opts?: Record<string, unknown>,
  ) => Promise<{ ok: boolean; missing: string[] }>;
};
const proxyRouteTable = require('../../../proxy/proxy.config.json') as {
  routes: Record<string, unknown>;
};

function proxyDescriptorFetch(name: string): Promise<unknown> {
  if (name === 'routes') return Promise.resolve(proxyRouteTable.routes);
  if (name === 'presets') return Promise.resolve({});
  return Promise.reject(new Error(`unexpected fetch: ${String(name)}`));
}

let routesChecked = false;

async function checkMuseRoutes(): Promise<void> {
  if (routesChecked) return;
  routesChecked = true;
  const result = await assertMuseRoutes(proxyDescriptorFetch);
  if (!result || result.ok !== true) {
    throw { code: 'upstream' };
  }
}

// Swap seam (AC-SAMPLE-026): the composition root injects the edge-proxy
// fetcher here; the T6 endpoint-swap proof swaps it with no component-code
// change. Undefined (default) keeps the stub proxy transport above.
let segmentRouteOverride: SegmentRouteFn | undefined;

export function setSegmentRoute(fn: SegmentRouteFn | undefined): void {
  segmentRouteOverride = fn;
  routesChecked = false;
}

function dispatchSegment(req: SegmentRouteRequest): {
  status: number;
  body: unknown;
} {
  if (segmentRouteOverride) return segmentRouteOverride(req);
  return stubRouteHandle({ route: 'segment', photoId: req.photoId, point: req.point });
}

// Memo cache keyed exactly by (photoId, point) (A-T5-3).
const memo = new Map<string, AnnotateResult>();

// Forced-error simulation for the manual error path: a photoId prefixed
// `force-<code>:` rejects once with that code; `retry()` re-fires the same
// call, which resolves once the (simulated) fixture recovers.
const FORCED_CODES: AnnotateErrorCode[] = ['auth', 'upstream', 'timeout', 'schema'];
const consumedForce = new Set<string>();

function memoKey(photoId: string, point: TapPoint): string {
  return `${photoId}:${point.x},${point.y}`;
}

function forcedCode(photoId: string): AnnotateErrorCode | undefined {
  const m = /^force-(auth|upstream|timeout|schema):/.exec(photoId);
  return m ? (m[1] as AnnotateErrorCode) : undefined;
}

function stripForcePrefix(photoId: string): string {
  return photoId.replace(/^force-(auth|upstream|timeout|schema):/, '');
}

function validPhotoId(photoId: unknown): photoId is string {
  return typeof photoId === 'string' && photoId.length > 0;
}

function validPoint(point: unknown): point is TapPoint {
  if (!point || typeof point !== 'object') return false;
  const p = point as { x: unknown; y: unknown };
  return (
    typeof p.x === 'number' &&
    typeof p.y === 'number' &&
    Number.isFinite(p.x) &&
    Number.isFinite(p.y) &&
    p.x >= 0 &&
    p.x <= SUGGESTION_VIEWPORT_PX &&
    p.y >= 0 &&
    p.y <= SUGGESTION_VIEWPORT_PX
  );
}

function toAnnotateError(err: unknown, retry: () => Promise<AnnotateResult>): AnnotateError {
  const code =
    err && typeof err === 'object' && 'code' in err && FORCED_CODES.includes((err as { code: AnnotateErrorCode }).code)
      ? (err as { code: AnnotateErrorCode }).code
      : 'upstream';
  return { code, retry };
}

function parsePolygon(value: unknown): Polygon {
  if (!Array.isArray(value) || value.length < 3) {
    throw { code: 'schema' };
  }
  for (const pt of value) {
    if (
      !Array.isArray(pt) ||
      pt.length !== 2 ||
      !pt.every((n) => typeof n === 'number' && Number.isFinite(n))
    ) {
      throw { code: 'schema' };
    }
  }
  return value as Polygon;
}

// annotateFromSegmentBody — reduces the recorded `segment` route body to the
// annotator shape: the mask is matches[0].mask, suggestions are the route's
// suggestion polygons (capped at MAX_SUGGESTIONS). Anything else is a schema
// rejection per RULE-MUSE-007 (never raw text).
function annotateFromSegmentBody(body: unknown): AnnotateResult {
  if (!body || typeof body !== 'object') throw { code: 'schema' };
  const doc = body as { matches?: unknown; suggestions?: unknown };
  if (!Array.isArray(doc.matches) || doc.matches.length === 0) throw { code: 'schema' };
  const first = doc.matches[0] as { mask?: { points?: unknown } };
  const mask = parsePolygon(first && first.mask ? first.mask.points : undefined);
  const rawSuggestions = Array.isArray(doc.suggestions) ? doc.suggestions : [];
  const suggestions = rawSuggestions
    .slice(0, MAX_SUGGESTIONS)
    .map((entry) => parsePolygon((entry as { points?: unknown }).points));
  return { mask, suggestions };
}

function schemaRejection(photoId: string, point: TapPoint): AnnotateError {
  return {
    code: 'schema',
    retry: () => annotatePhoto(photoId, point),
  };
}

// annotatePhoto(photoId, point) — tap → mask + up to 3 suggestions.
// Resolves via the `segment` route/stub; rejects `{code, retry}` per
// RULE-MUSE-007 (never raw text). Results are memoized by photoId+point, so
// promote and retry reuse the polygon with no second `segment` call.
export async function annotatePhoto(photoId: string, point: TapPoint): Promise<AnnotateResult> {
  if (!validPhotoId(photoId) || !validPoint(point)) {
    throw schemaRejection(photoId as string, point as TapPoint);
  }
  const key = memoKey(photoId, point);
  const hit = memo.get(key);
  if (hit) return hit;

  const code = forcedCode(photoId);
  if (code && !consumedForce.has(key)) {
    consumedForce.add(key);
    const err: AnnotateError = { code, retry: () => annotatePhoto(photoId, point) };
    throw err;
  }

  try {
    await checkMuseRoutes();
  } catch (err) {
    throw toAnnotateError(err, () => annotatePhoto(photoId, point));
  }

  let res: { status: number; body: unknown };
  try {
    // The forced-error prefix is a demo-side manual path marker, not part of
    // the photo identity: the retry dispatches the stripped photoId so the
    // `segment` route resolves once the (simulated) fixture recovers.
    res = dispatchSegment({ route: 'segment', photoId: stripForcePrefix(photoId), point });
  } catch (err) {
    throw toAnnotateError(err, () => annotatePhoto(photoId, point));
  }
  if (!res || res.status !== 200) {
    throw toAnnotateError({ code: 'upstream' }, () => annotatePhoto(photoId, point));
  }

  let result: AnnotateResult;
  try {
    result = annotateFromSegmentBody(res.body);
  } catch (err) {
    throw toAnnotateError(err, () => annotatePhoto(photoId, point));
  }
  memo.set(key, result);
  return result;
}

// promoteSuggestion(suggestions, index) — promote reuses the polygon: the
// returned polygon is the identical reference, with no re-segment call.
export function promoteSuggestion(suggestions: Polygon[], index: number): Polygon {
  if (!Array.isArray(suggestions) || suggestions.length === 0) {
    throw new Error('promoteSuggestion: no suggestions');
  }
  const pick = suggestions[index];
  if (!Array.isArray(pick)) throw new Error('promoteSuggestion: bad index');
  return pick;
}

// Settle-fired helper for the Lens tab: restarts the window on every tap and
// fires `annotatePhoto` once the tap settles. Returns a cancel function.
export function requestOnSettle(
  photoId: string,
  point: TapPoint,
  onDone: (result: AnnotateResult) => void,
  onError: (err: AnnotateError) => void,
  delayMs: number = SETTLE_MS,
): () => void {
  const timer = setTimeout(() => {
    annotatePhoto(photoId, point).then(onDone, onError);
  }, delayMs);
  return () => clearTimeout(timer);
}

// Test/manual-reset hook: clears memo + forced-error state.
export function clearAnnotateCache(): void {
  memo.clear();
  consumedForce.clear();
  routesChecked = false;
}
