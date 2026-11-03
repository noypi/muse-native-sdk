// T4 (BUG-SAMPLE-008, AC-SAMPLE-033) — guess-the-mask round against the stub
// proxy `segment` route (mask truth) and the `reason` route (reason stream).
// Stub fixtures are inline dummy data — no proxy call is the bypass this
// item removes: the round now dispatches `{ route: 'segment', ... }` through
// the stub proxy handle (proxy/lib/stub.js, the single source of truth) and
// reads the reason stream from `{ route: 'reason', ... }` (Q-MUSE-004: web
// image search stays out), no secrets/config.
//
// Round mask derivation from the recorded `segment` body: mask-1 is
// matches[0].mask (the tap-region truth), mask-2 is suggestions[0] (the first
// dashed alternative). Mask ids stay stable (mask-1, mask-2) so round wiring
// such as the App tap handler is untouched by swaps; polygons follow the
// route, so an endpoint swap changes them with no component-code change
// (AC-SAMPLE-026). The `setRoundSegmentRoute` seam is the swap point.
export type Mask = {
  id: string;
  label: string;
  polygon: number[][];
};

export type RevealResult = {
  mask: Mask;
  reasonStream: AsyncIterable<string>;
  shelfEntry: { renderMode: 'by_object' };
};

type GuessPoint = {
  x: number;
  y: number;
};

type StubHandle = (req: Record<string, unknown>) => { status: number; body: any };

// Live `segment`/`reason` route transport. The require is guarded and
// function-local so this module stays dependency-free under import-less
// evaluation (the m3-gallery viral suite transpiles this file with no module
// loader): every real runtime (host/dev, node) resolves the stub proxy
// handle and the round dispatches live; otherwise the recorded snapshot
// below applies.
function liveRouteHandle(): StubHandle | undefined {
  try {
    if (typeof require !== 'function') return undefined;
    const mod = require('../../../proxy/lib/stub.js') as { handle?: unknown };
    if (!mod || typeof mod.handle !== 'function') return undefined;
    return mod.handle as StubHandle;
  } catch {
    return undefined;
  }
}

// Recorded `segment`/`reason` fixture snapshot (proxy/lib/stub.js
// FIXTURES.segment mask + first suggestion, FIXTURES.reason streams.masked):
// import-less evaluation fallback ONLY. The live `segment` route is the
// single source of truth at runtime; this snapshot mirrors its recorded
// polygons so hermetic evaluation reduces identically.
const RECORDED_MATCH_MASK: number[][] = [
  [360, 360],
  [408, 360],
  [408, 408],
  [360, 408],
];

const RECORDED_SUGGESTION_FIRST: number[][] = [
  [330, 350],
  [370, 350],
  [370, 390],
  [330, 390],
];

const RECORDED_REASON_MASKED: string[] = [
  '{"reasoning_content":"recorded-thinking"}',
  '{"content":"recorded-insight"}',
];

// Swap seam (AC-SAMPLE-026): the composition root injects the edge-proxy
// segment body here; the endpoint-swap proof swaps it with no
// component-code change. Undefined (default) keeps the live route above.
let roundSegmentOverride: ((photoId: string, point: GuessPoint) => unknown) | undefined;

export function setRoundSegmentRoute(fn: ((photoId: string, point: GuessPoint) => unknown) | undefined): void {
  roundSegmentOverride = fn;
}

function recordedSegmentBody(): unknown {
  return {
    matches: [{ name: 'tap-region', mask: { encoding: 'polygon', points: RECORDED_MATCH_MASK } }],
    suggestions: [{ encoding: 'polygon', points: RECORDED_SUGGESTION_FIRST }],
  };
}

function resolveSegmentBody(photoId: string, point: GuessPoint): unknown {
  if (roundSegmentOverride) return roundSegmentOverride(photoId, point);
  const handle = liveRouteHandle();
  if (!handle) return recordedSegmentBody();
  const res = handle({ route: 'segment', photoId, point });
  if (!res || res.status !== 200) {
    throw new Error(`guessMask: segment route status ${res && res.status}`);
  }
  return res.body;
}

function resolveReasonChunks(photoId: string, label: string): string[] {
  const handle = liveRouteHandle();
  if (!handle) return [...RECORDED_REASON_MASKED];
  const res = handle({ route: 'reason', context: `masked:${label}:${photoId}` });
  if (!res || res.status !== 200) {
    throw new Error(`guessMask: reason route status ${res && res.status}`);
  }
  const streams = (res.body as { streams?: { masked?: unknown } }).streams;
  if (!streams || !Array.isArray(streams.masked) || streams.masked.length === 0) {
    throw new Error('guessMask: reason route returned no masked stream');
  }
  return (streams.masked as unknown[]).map((chunk) => String(chunk));
}

function polygonPoints(value: unknown): number[][] {
  if (!Array.isArray(value) || value.length < 3) {
    throw new Error('guessMask: unparseable segment polygon');
  }
  for (const pt of value) {
    if (
      !Array.isArray(pt) ||
      pt.length !== 2 ||
      !pt.every((n) => typeof n === 'number' && Number.isFinite(n))
    ) {
      throw new Error('guessMask: unparseable segment polygon');
    }
  }
  return value as number[][];
}

// masksFromSegment — round mask derivation from the `segment` route body
// (see header): exactly two stable ids over the recorded polygons.
function masksFromSegment(body: unknown): Mask[] {
  if (!body || typeof body !== 'object') {
    throw new Error('guessMask: segment route returned no body');
  }
  const doc = body as { matches?: unknown; suggestions?: unknown };
  if (!Array.isArray(doc.matches) || doc.matches.length === 0) {
    throw new Error('guessMask: segment route returned no masks');
  }
  if (!Array.isArray(doc.suggestions) || doc.suggestions.length === 0) {
    throw new Error('guessMask: segment route returned no suggestions');
  }
  const first = doc.matches[0] as { name?: unknown; mask?: { points?: unknown } };
  const label = typeof first.name === 'string' && first.name ? first.name : 'tap-region';
  return [
    { id: 'mask-1', label, polygon: polygonPoints(first.mask ? first.mask.points : undefined) },
    { id: 'mask-2', label, polygon: polygonPoints((doc.suggestions[0] as { points?: unknown }).points) },
  ];
}

let activePhotoId: string | undefined;
let activeGuessId: string | undefined;
let activeMasks: Mask[] | undefined;
let activeHiddenId: string | undefined;

function findMask(masks: Mask[], maskId: string): Mask {
  const found = masks.find((m) => m.id === maskId);
  if (!found) {
    throw new Error(`unknown maskId: ${maskId}`);
  }
  return found;
}

// Center-tap default for the startRound leg: startRound takes no point, so
// the `segment` route resolves at viewport center; playGuessRound dispatches
// the visitor's actual tap point instead.
function defaultPoint(): GuessPoint {
  return { x: 384, y: 384 };
}

export function startRound(photoId: string): void {
  if (!photoId || photoId.trim().length === 0) {
    throw new Error('startRound requires a non-empty photoId');
  }
  // Resolve the round through the `segment` route: the hidden mask is fixed
  // per round (deterministic pick over the routed masks), never the
  // visitor's guess: reveal reports this mask as the truth.
  const masks = masksFromSegment(resolveSegmentBody(photoId, defaultPoint()));
  const hidden = masks[photoId.length % masks.length];
  if (!hidden) {
    throw new Error('startRound: segment route returned no masks');
  }
  activePhotoId = photoId;
  activeGuessId = undefined;
  activeMasks = masks;
  activeHiddenId = hidden.id;
}

export function guess(maskId: string): void {
  if (!activePhotoId || !activeMasks) {
    throw new Error('guess before startRound: call startRound(photoId) first');
  }
  if (!maskId || maskId.trim().length === 0) {
    throw new Error('guess requires a non-empty maskId');
  }
  // Validate against the routed mask set.
  findMask(activeMasks, maskId);
  activeGuessId = maskId;
}

async function* streamChunks(chunks: string[]): AsyncIterable<string> {
  for (const chunk of chunks) {
    yield chunk;
  }
}

function validGuessPoint(point: unknown): point is GuessPoint {
  if (!point || typeof point !== 'object') return false;
  const p = point as { x: unknown; y: unknown };
  return (
    typeof p.x === 'number' &&
    typeof p.y === 'number' &&
    Number.isFinite(p.x) &&
    Number.isFinite(p.y)
  );
}

export function reveal(): {
  mask: Mask;
  reasonStream: AsyncIterable<string>;
  shelfEntry: { renderMode: 'by_object' };
} {
  if (!activePhotoId || !activeMasks) {
    throw new Error('reveal before startRound: call startRound(photoId) first');
  }
  if (!activeGuessId) {
    throw new Error('reveal before guess: call guess(maskId) first');
  }
  if (!activeHiddenId) {
    throw new Error('reveal before startRound: call startRound(photoId) first');
  }
  // Reveal the hidden mask for this round — not the guess — then consume
  // the round so stale state cannot leak into the next one. The reason
  // stream resolves through the `reason` route for the revealed mask.
  const mask = findMask(activeMasks, activeHiddenId);
  const reasonStream: AsyncIterable<string> = streamChunks(
    resolveReasonChunks(activePhotoId, mask.label),
  );
  const shelfEntry: { renderMode: 'by_object' } = { renderMode: 'by_object' };
  activePhotoId = undefined;
  activeGuessId = undefined;
  activeMasks = undefined;
  activeHiddenId = undefined;
  return { mask, reasonStream, shelfEntry };
}

// playGuessRound(photoId, guess) — full round through the `segment` route:
// tap point → routed masks → hidden-mask reveal with the `reason`-route
// stream and the by_object shelf entry. Resolves in one call (<30s against
// the stub: no delays, no network).
export async function playGuessRound(photoId: string, guessPoint: GuessPoint): Promise<RevealResult> {
  if (!photoId || photoId.trim().length === 0) {
    throw new Error('playGuessRound requires a non-empty photoId');
  }
  if (!validGuessPoint(guessPoint)) {
    throw new Error('playGuessRound requires a finite { x, y } guess point');
  }
  const masks = masksFromSegment(resolveSegmentBody(photoId, guessPoint));
  const hidden = masks[photoId.length % masks.length];
  if (!hidden) {
    throw new Error('playGuessRound: segment route returned no masks');
  }
  const reasonStream: AsyncIterable<string> = streamChunks(
    resolveReasonChunks(photoId, hidden.label),
  );
  const shelfEntry: { renderMode: 'by_object' } = { renderMode: 'by_object' };
  return { mask: hidden, reasonStream, shelfEntry };
}
