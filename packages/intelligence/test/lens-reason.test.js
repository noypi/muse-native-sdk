'use strict';
// T3 — MuseLens crop→reason streaming (AC-MUSE-009) + states (AC-MUSE-010).
// Hermetic: dispatch boundary faked via require-cache seeding; SSE-style
// reasoning_content deltas replayed in-process. No network, no keys.
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

const POLYGON = [[10, 10], [100, 10], [100, 100], [10, 100]];

beforeEach(() => {
  delete require.cache[DISPATCH_PATH];
  delete require.cache[path.resolve(__dirname, '../lib/lens.js')];
});

describe('reasonAboutMask — reasoning stream then content (AC-MUSE-009)', () => {
  it('calls reason route with include_reasoning:true + declared effort', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: { content: 'A kettle.' } };
    });
    const { reasonAboutMask } = freshLens();
    await reasonAboutMask({
      polygon: POLYGON,
      imageUrl: 'https://img.test/k.jpg',
      prompt: 'Name this object.',
      reasoningEffort: 'medium',
      onThinking: () => {},
    });
    assert.equal(seen.length, 1);
    assert.equal(seen[0].route, 'reason');
    assert.equal(seen[0].include_reasoning, true);
    assert.equal(seen[0].reasoningEffort || (seen[0].params && seen[0].params.reasoning_effort), 'medium');
  });

  it('streams reasoning_content deltas to onThinking in order, then resolves content', async () => {
    seedDispatchFake(async (req, ctx) => {
      for (const d of ['Thinking about ', 'the kettle…']) {
        if (ctx && typeof ctx.onDelta === 'function') ctx.onDelta({ reasoning_content: d });
      }
      return { status: 200, body: { content: 'A kettle boils water.' } };
    });
    const { reasonAboutMask } = freshLens();
    const deltas = [];
    const res = await reasonAboutMask({
      polygon: POLYGON,
      imageUrl: 'https://img.test/k.jpg',
      prompt: 'Name this.',
      onThinking: (d) => deltas.push(d),
    });
    // ASSUMPTION: fake replays deltas synchronously through ctx.onDelta;
    // real impl wires SSE reasoning_content chunks to onThinking in arrival order.
    assert.deepEqual(deltas, ['Thinking about ', 'the kettle…']);
    assert.equal(res.content, 'A kettle boils water.');
    assert.equal(res.route, 'reason');
    assert.equal(typeof res.latencyMs, 'number');
  });

  it('surfaces only proxy taxonomy errors (never raw upstream)', async () => {
    const proxyErr = new Error('boom');
    proxyErr.code = 'timeout';
    seedDispatchFake(async () => { throw proxyErr; });
    const { reasonAboutMask } = freshLens();
    await assert.rejects(
      reasonAboutMask({ polygon: POLYGON, imageUrl: 'https://img.test/k.jpg', prompt: 'Name this.', onThinking: () => {} }),
      (err) => err && ['auth', 'upstream', 'timeout', 'schema'].includes(err.code),
    );
  });

  it('rejects invalid reasoningEffort before dispatch', async () => {
    let called = 0;
    seedDispatchFake(async () => {
      called += 1;
      return { status: 200, body: { content: 'x' } };
    });
    const { reasonAboutMask } = freshLens();
    await assert.rejects(
      reasonAboutMask({
        polygon: POLYGON, imageUrl: 'https://img.test/k.jpg', prompt: 'x',
        reasoningEffort: 'ultra', onThinking: () => {},
      }),
    );
    assert.equal(called, 0);
  });
});
