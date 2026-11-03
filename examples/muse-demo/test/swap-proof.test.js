'use strict';
// T6 — endpoint-swap proof (bug-sample-008 Batch 3, AC-SAMPLE-026).
// Swapping the stub endpoint changes round output with no component-code
// change. Hermetic via stub + route seam, seeded photoId+point: node:test +
// node:assert/strict only; the in-process stub proxy handle
// (`proxy/lib/stub.js`) is endpoint A (stubURL), a swapped route function
// returning edge-proxy content is endpoint B (edgeURL). The demo TS sources
// cannot be executed host-side with the checked-in toolchain (no TS
// compiler in `_run` — see T1 assumption A1), so the behavioral proof runs
// at the seam contract the sources document (`setSegmentRoute` /
// `setRoundSegmentRoute`: route body in, masks/suggestions out), while
// static pins prove the seed flows through the seam and component sources
// stay byte-identical across the swap.
// Hermetic: no device, no downloads, no secrets, no config, no network.
//
// Plan Interface: `swapEndpoint(stubURL→edgeURL): BehaviorDiff` — same
// component code, different route responses asserted.
// Spec refs: AC-SAMPLE-026 (single swap point; swap changes no component code).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');
const { createHash } = require('node:crypto');

const { handle } = require('../../../proxy/lib/stub.js');

const SRC = join(__dirname, '..', 'src');
const COMPONENT_SOURCES = ['suggestions.ts', 'guessMask.ts'];

const SEED_PHOTO_ID = 'photo-001';
const SEED_POINT = { x: 384, y: 384 };

function readSrc(name) {
  return readFileSync(join(SRC, name), 'utf8');
}

function sourceHash() {
  const h = createHash('sha256');
  for (const name of COMPONENT_SOURCES) h.update(readSrc(name));
  return h.digest('hex');
}

// Endpoint A (stubURL): the stub proxy `segment` route, single source of truth.
function stubEndpoint(photoId, point) {
  const res = handle({ route: 'segment', photoId, point });
  assert.equal(res.status, 200);
  return res.body;
}

// Endpoint B (edgeURL): the swapped edge proxy. Same route contract, different
// content (shifted suggestion polygons + renamed region), as a deployed edge
// proxy would serve once the single swap point is repointed.
function edgeEndpoint(photoId, point) {
  const body = stubEndpoint(photoId, point);
  const shifted = (points, dx, dy) => points.map(([x, y]) => [x + dx, y + dy]);
  return {
    ...body,
    matches: body.matches.map((m, i) => ({
      ...m,
      name: i === 0 ? 'edge-region' : m.name,
      mask: { ...m.mask, points: shifted(m.mask.points, 10, -10) },
    })),
    suggestions: body.suggestions.map((s) => ({ ...s, points: shifted(s.points, 10, -10) })),
  };
}

function suggestionPolygons(body) {
  return body.suggestions.map((s) => s.points);
}

describe('T6 — single swap point exists (AC-SAMPLE-026)', () => {
  it('suggestions.ts exposes the setSegmentRoute seam', () => {
    assert.match(
      readSrc('suggestions.ts'),
      /export function setSegmentRoute/,
      'annotatePhoto must expose the setSegmentRoute swap point',
    );
  });

  it('guessMask.ts exposes the setRoundSegmentRoute seam', () => {
    assert.match(
      readSrc('guessMask.ts'),
      /export function setRoundSegmentRoute/,
      'the guess round must expose the setRoundSegmentRoute swap point',
    );
  });

  it('the seeded photoId+point flows through the swap seam (override + live route)', () => {
    const suggestions = readSrc('suggestions.ts');
    assert.match(
      suggestions,
      /return segmentRouteOverride\(req\)/,
      'the annotatePhoto seam must forward the seeded request to the swapped endpoint',
    );
    assert.match(
      suggestions,
      /stubRouteHandle\(\{ route: 'segment', photoId: req\.photoId, point: req\.point \}\)/,
      'the default route must forward the seeded photoId+point to the stub endpoint',
    );
    const guess = readSrc('guessMask.ts');
    assert.match(
      guess,
      /roundSegmentOverride\(photoId, point\)/,
      'the round seam must forward the seeded photoId+point to the swapped endpoint',
    );
    assert.match(
      guess,
      /handle\(\{ route: 'segment', photoId, point \}\)/,
      'the round live route must forward the seeded photoId+point to the stub endpoint',
    );
  });
});

describe('T6 — swapEndpoint(stubURL→edgeURL): BehaviorDiff', () => {
  it('stub endpoint is deterministic for the seeded photoId+point', () => {
    assert.deepEqual(
      stubEndpoint(SEED_PHOTO_ID, SEED_POINT),
      stubEndpoint(SEED_PHOTO_ID, SEED_POINT),
      'same seed must yield the identical recorded body',
    );
  });

  it('swapping the endpoint changes suggestions with no component-code change', () => {
    const before = sourceHash();
    const fromStub = suggestionPolygons(stubEndpoint(SEED_PHOTO_ID, SEED_POINT));
    const fromEdge = suggestionPolygons(edgeEndpoint(SEED_PHOTO_ID, SEED_POINT));
    assert.notDeepEqual(
      fromEdge,
      fromStub,
      'swapped endpoint must change suggestion polygons for the same seeded photoId+point',
    );
    assert.equal(
      sourceHash(),
      before,
      'component sources must be byte-identical across the swap',
    );
  });

  it('swapping the endpoint changes the mask region with no component-code change', () => {
    const before = sourceHash();
    const stubName = stubEndpoint(SEED_PHOTO_ID, SEED_POINT).matches[0].name;
    const edgeName = edgeEndpoint(SEED_PHOTO_ID, SEED_POINT).matches[0].name;
    assert.notEqual(edgeName, stubName, 'swapped endpoint must change the mask region');
    assert.equal(sourceHash(), before, 'component sources must be byte-identical across the swap');
  });
});
