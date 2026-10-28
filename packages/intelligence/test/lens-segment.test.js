'use strict';
// T2 — MuseLens tap→segment polygon pipeline (AC-MUSE-008).
// Hermetic: dispatch boundary is faked in-process via require-cache seeding
// of proxy/lib/dispatch.js BEFORE lens.js loads, so no network and no keys.
// Uses node:test + node:assert.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const DISPATCH_PATH = path.resolve(__dirname, '../../../proxy/lib/dispatch.js');

function seedDispatchFake(fake) {
  require.cache[DISPATCH_PATH] = {
    id: DISPATCH_PATH,
    filename: DISPATCH_PATH,
    loaded: true,
    exports: { dispatch: fake },
  };
}

function freshLens() {
  delete require.cache[path.resolve(__dirname, '../lib/lens.js')];
  return require('../lib/lens.js');
}

const GOOD_MASK = {
  matches: [
    {
      box: [10, 10, 100, 100],
      mask: { encoding: 'polygon', points: [[10, 10], [100, 10], [100, 100], [10, 100]] },
      confidence: 0.94,
      name: 'kettle',
      insight: 'A kettle boils water.',
    },
  ],
};

beforeEach(() => {
  delete require.cache[DISPATCH_PATH];
  delete require.cache[path.resolve(__dirname, '../lib/lens.js')];
});

describe('segmentAt — tap→segment polygon (AC-MUSE-008)', () => {
  it('sends imageUrl+point+mask_encoding:polygon to the segment route', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: GOOD_MASK };
    });
    const { segmentAt } = freshLens();
    const res = await segmentAt({ imageUrl: 'https://img.test/k.jpg', x: 42, y: 17, prompt: 'Name this.' });
    assert.equal(seen.length, 1);
    assert.equal(seen[0].route, 'segment');
    assert.deepEqual(seen[0].point, [42, 17]);
    assert.equal(seen[0].mask_encoding, 'polygon');
    assert.equal(seen[0].imageUrl, 'https://img.test/k.jpg');
    assert.ok(Array.isArray(res.polygon) && res.polygon.length === 4);
    assert.equal(res.name, 'kettle');
    assert.equal(res.insight, 'A kettle boils water.');
    assert.equal(typeof res.confidence, 'number');
  });

  it('returns route + latencyMs alongside the payload', async () => {
    seedDispatchFake(async () => ({ status: 200, body: GOOD_MASK }));
    const { segmentAt } = freshLens();
    const res = await segmentAt({ imageUrl: 'https://img.test/k.jpg', x: 1, y: 2, prompt: 'Name this.' });
    assert.equal(res.route, 'segment');
    assert.equal(typeof res.latencyMs, 'number');
  });

  it('forwards preset key when given (preset-name-only)', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: GOOD_MASK };
    });
    const { segmentAt } = freshLens();
    await segmentAt({ imageUrl: 'https://img.test/k.jpg', x: 1, y: 2, prompt: 'Name this.', preset: 'lens-fast' });
    assert.equal(seen[0].preset, 'lens-fast');
  });

  it('unparseable mask throws MuseError schema/unparseable-mask (never silent empty)', async () => {
    seedDispatchFake(async () => ({ status: 200, body: { matches: [{ mask: { encoding: 'rle', counts: 'zzz' } }] } }));
    const { segmentAt } = freshLens();
    await assert.rejects(
      segmentAt({ imageUrl: 'https://img.test/k.jpg', x: 1, y: 2, prompt: 'Name this.' }),
      (err) => err && err.code === 'schema' && /unparseable-mask/.test(err.reason || err.message || ''),
    );
  });

  it('empty matches throws MuseError schema/unparseable-mask', async () => {
    seedDispatchFake(async () => ({ status: 200, body: { matches: [] } }));
    const { segmentAt } = freshLens();
    await assert.rejects(
      segmentAt({ imageUrl: 'https://img.test/k.jpg', x: 1, y: 2, prompt: 'Name this.' }),
      (err) => err && err.code === 'schema',
    );
  });

  it('surfaces only proxy taxonomy errors from dispatch', async () => {
    const proxyErr = new Error('upstream blew up');
    proxyErr.code = 'upstream';
    seedDispatchFake(async () => { throw proxyErr; });
    const { segmentAt } = freshLens();
    await assert.rejects(
      segmentAt({ imageUrl: 'https://img.test/k.jpg', x: 1, y: 2, prompt: 'Name this.' }),
      (err) => err && ['auth', 'upstream', 'timeout', 'schema'].includes(err.code),
    );
  });
});
