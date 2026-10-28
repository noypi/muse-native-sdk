// T5 — suggestion pipeline for the muse-demo photo annotator (AC-SAMPLE-023).
// Pure logic: segment-route binding, settle-fired auto-suggestions (768px,
// memoized by photoId+point), and promote-reuses-polygon. Rendering lives in
// `./annotator.tsx`; shelving lives in `./listShelf.tsx`.

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

// Deterministic 32-bit hash of the memo key; seeds the recorded-style
// polygon so the same tap always yields the same mask (stub-backed in the
// running demo via the `segment` route — see proxy/lib/stub.js FIXTURES).
function hashKey(key: string): number {
  let h = 2166136261;
  for (let i = 0; i < key.length; i += 1) {
    h ^= key.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

function ringAround(x: number, y: number, radius: number, sides: number, phase: number): Polygon {
  const pts: Polygon = [];
  for (let i = 0; i < sides; i += 1) {
    const a = phase + (i / sides) * Math.PI * 2;
    pts.push([
      Math.round((x + radius * Math.cos(a)) * 10) / 10,
      Math.round((y + radius * Math.sin(a)) * 10) / 10,
    ]);
  }
  return pts;
}

// Recorded-style segment body: mirrors the stub `segment` fixture shape
// (`matches[0].mask` polygon + up to 3 suggestion polygons).
function segmentBody(photoId: string, point: TapPoint): AnnotateResult {
  const h = hashKey(memoKey(photoId, point));
  const radius = 24 + (h % 40);
  const phase = ((h >> 8) % 360) * (Math.PI / 180);
  const mask = ringAround(point.x, point.y, radius, 8, phase);
  const suggestions: Polygon[] = [];
  const count = 1 + ((h >> 16) % MAX_SUGGESTIONS);
  for (let i = 0; i < count; i += 1) {
    const dx = ((h >> (i * 5)) % 60) - 30;
    const dy = ((h >> (i * 5 + 2)) % 60) - 30;
    suggestions.push(
      ringAround(point.x + dx, point.y + dy, Math.max(12, radius - 6 * (i + 1)), 6, phase + i),
    );
  }
  return { mask, suggestions: suggestions.slice(0, MAX_SUGGESTIONS) };
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
export function annotatePhoto(photoId: string, point: TapPoint): Promise<AnnotateResult> {
  if (!validPhotoId(photoId) || !validPoint(point)) {
    return Promise.reject(schemaRejection(photoId as string, point as TapPoint));
  }
  const key = memoKey(photoId, point);
  const hit = memo.get(key);
  if (hit) return Promise.resolve(hit);

  const code = forcedCode(photoId);
  if (code && !consumedForce.has(key)) {
    consumedForce.add(key);
    const err: AnnotateError = { code, retry: () => annotatePhoto(photoId, point) };
    return Promise.reject(err);
  }

  const result = segmentBody(photoId, point);
  memo.set(key, result);
  return Promise.resolve(result);
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
}
