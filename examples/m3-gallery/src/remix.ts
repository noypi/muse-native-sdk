// Gallery remix shuffle + gallery:// deep link (AC-SAMPLE-031).
// Builds on the stories.js registry (FR-SAMPLE-001): shuffle picks a fresh
// remix for the current story, encode/decode carry it through a shareable link.

export interface Remix {
  seed: number;
  palette: string;
  shapeScale: number;
}

export interface RemixParams {
  id: string;
  seed: number;
}

const PALETTES = ['vivid', 'muted', 'mono', 'sunset'];

function randomSeed(): number {
  return Math.floor(Math.random() * 1_000_000_000);
}

export function shuffleRemix(): { seed: number; palette: string; shapeScale: number } {
  const seed = randomSeed();
  const palette = PALETTES[seed % PALETTES.length] as string;
  const shapeScale = 0.5 + ((seed % 1000) / 1000) * 1.5;
  const remix: Remix = { seed, palette, shapeScale };
  return remix;
}

export function encodeRemix(id: string, seed: number): string {
  if (typeof id !== 'string' || id.length === 0) {
    throw new Error('encodeRemix: story id must be a non-empty string');
  }
  if (!Number.isInteger(seed)) {
    throw new Error('encodeRemix: seed must be an integer');
  }
  // Encode the id so link metacharacters (?, #, /) cannot break the scheme.
  return `gallery://story/${encodeURIComponent(id)}?seed=${seed}`;
}

export function decodeRemix(link: string): RemixParams {
  if (typeof link !== 'string' || !link.startsWith('gallery://')) {
    throw new Error('decodeRemix: link must use the gallery:// scheme');
  }
  const prefix = 'gallery://story/';
  if (!link.startsWith(prefix)) {
    throw new Error('decodeRemix: malformed gallery link (expected gallery://story/<id>?seed=<n>)');
  }
  const rest = link.slice(prefix.length);
  const qIndex = rest.indexOf('?');
  if (qIndex < 0) {
    throw new Error('decodeRemix: malformed gallery link (missing seed query param)');
  }
  const rawId = rest.slice(0, qIndex);
  const query = rest.slice(qIndex + 1);
  const params = new URLSearchParams(query);
  const seedRaw = params.get('seed');
  if (rawId.length === 0 || seedRaw === null || seedRaw.length === 0) {
    throw new Error('decodeRemix: malformed gallery link (id and seed are required)');
  }
  const seed = Number(seedRaw);
  if (!Number.isInteger(seed)) {
    throw new Error('decodeRemix: malformed gallery link (seed must be an integer)');
  }
  const id = decodeURIComponent(rawId);
  if (id.length === 0) {
    throw new Error('decodeRemix: malformed gallery link (id must be non-empty)');
  }
  const remixParams: RemixParams = { id, seed };
  // decode(encode(x)) reproduces the exact remix: seed survives the round-trip
  // through the gallery://story/ scheme via the shared seed carrier.
  void remixParams.seed;
  return remixParams;
}
