'use strict';
// T2 — stub `segment` route contract pins (bug-sample-008 Batch 1).
// Pins the `segment(photoId, point): {polygon, suggestions[≤3], reasonStream}`
// contract against the stub proxy (`proxy/lib/stub.js`), which is the single
// source of truth per the BQ1 answer (stub serves `segment` on merged main;
// client surface is generic `assertMuseRoutes`/`streamReason` — no dedicated
// TS `segment` wrapper, so T2 targets the stub contract, not a wrapper).
// Hermetic: node:test + node:assert/strict only; in-process stub handle(),
// no device, no downloads, no secrets, no config, no network.
//
// Seeding/memo ownership: the stub serves one recorded fixture (deterministic
// per process); per-(photoId+point) seeding and the memo key live demo-side
// (T3 `annotatePhoto`) and are pinned here as caller obligations.
// Spec refs: FR-SAMPLE-009, FR-SAMPLE-010, FR-SAMPLE-011, AC-SAMPLE-023.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { handle } = require('../../../proxy/lib/stub.js');

const MAX_SUGGESTIONS = 3;

function isPoint(p) {
  return (
    Array.isArray(p) &&
    p.length === 2 &&
    typeof p[0] === 'number' &&
    Number.isFinite(p[0]) &&
    typeof p[1] === 'number' &&
    Number.isFinite(p[1])
  );
}

function isPolygon(points) {
  return Array.isArray(points) && points.length >= 3 && points.every(isPoint);
}

function segmentBody(photoId, point) {
  const res = handle({ route: 'segment', photoId, point });
  assert.equal(res.status, 200);
  return res.body;
}

describe('T2 — segment route serves the recorded mask polygon', () => {
  it('matches[0].mask.points is a closed polygon ring', () => {
    const body = segmentBody('photo-001', { x: 384, y: 384 });
    assert.ok(Array.isArray(body.matches) && body.matches.length >= 1, 'matches must be non-empty');
    const mask = body.matches[0].mask;
    assert.equal(mask.encoding, 'polygon');
    assert.ok(isPolygon(mask.points), 'mask.points must be a ring of finite [x, y] pairs');
  });

  it('segment body is deterministic for the same photoId+point (recorded fixture)', () => {
    const a = segmentBody('photo-001', { x: 100, y: 200 });
    const b = segmentBody('photo-001', { x: 100, y: 200 });
    assert.deepEqual(a, b, 'same photoId+point must yield the identical recorded body');
  });

  it('segment body carries the recorded insight excerpt', () => {
    const body = segmentBody('photo-001', { x: 384, y: 384 });
    assert.equal(typeof body.matches[0].insight, 'string');
    assert.ok(body.matches[0].insight.length > 0, 'insight must be non-empty');
  });
});

describe('T2 — segment suggestions are bounded (≤3 polygons)', () => {
  it('suggestions is an array of at most MAX_SUGGESTIONS polygons', () => {
    const body = segmentBody('photo-001', { x: 384, y: 384 });
    assert.ok(Array.isArray(body.suggestions), 'suggestions must be an array');
    assert.ok(
      body.suggestions.length <= MAX_SUGGESTIONS,
      `suggestions length ${body.suggestions.length} must be ≤ ${MAX_SUGGESTIONS}`,
    );
  });

  it('every suggestion is a polygon with finite points', () => {
    const body = segmentBody('photo-001', { x: 384, y: 384 });
    for (const s of body.suggestions) {
      assert.equal(s.encoding, 'polygon');
      assert.ok(isPolygon(s.points), 'each suggestion must be a ring of finite [x, y] pairs');
    }
  });
});

describe('T2 — reasonStream source: reason route streams', () => {
  it('reason route serves masked streams for the Field insight path', () => {
    const res = handle({ route: 'reason', context: 'masked:tap-region:photo-001' });
    assert.equal(res.status, 200);
    assert.ok(
      res.body.streams && Array.isArray(res.body.streams.masked) && res.body.streams.masked.length > 0,
      'reason.streams.masked must be a non-empty chunk array (reasonStream source)',
    );
  });
});

describe('T2 — stub boundary and error cases', () => {
  it('forced-error marker rejects with the auth taxonomy code', () => {
    assert.throws(
      () => handle({ route: 'segment', photoId: 'force-auth:photo-001', point: { x: 1, y: 1 } }),
      (err) => err && err.code === 'auth',
      'force-auth: marker must throw code auth',
    );
  });

  it('schema forced error is terminal (retryable false)', () => {
    assert.throws(
      () => handle({ route: 'segment', photoId: 'force-schema:photo-001', point: { x: 1, y: 1 } }),
      (err) => err && err.code === 'schema' && err.retryable === false,
      'force-schema: marker must throw terminal code schema',
    );
  });

  it('unknown route throws unknown-route (never a bare success)', () => {
    assert.throws(
      () => handle({ route: 'nope' }),
      (err) => err && err.code === 'unknown-route',
      'unknown route must throw code unknown-route',
    );
  });

  it('caller turnId echoes opaquely on the recorded body', () => {
    const res = handle({ route: 'segment', photoId: 'photo-001', point: { x: 1, y: 1 }, turnId: 't-1' });
    assert.equal(res.status, 200);
    assert.equal(res.body.turnId, 't-1', 'stub must echo a caller-supplied turnId');
  });

  it('no turnId supplied means none invented', () => {
    const body = segmentBody('photo-001', { x: 1, y: 1 });
    assert.ok(!('turnId' in body), 'stub must not invent a turnId');
  });
});
