'use strict';
// T7 — MuseList structureData: strict-schema request per renderMode (AC-MUSE-014).
// Hermetic: dispatch boundary is faked in-process via require-cache seeding
// of proxy/lib/dispatch.js BEFORE list.js loads, so no network and no keys.
// Uses node:test + node:assert.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const DISPATCH_PATH = path.resolve(__dirname, '../../../proxy/lib/dispatch.js');
const LIST_PATH = path.resolve(__dirname, '../lib/list.js');

function seedDispatchFake(fake) {
  require.cache[DISPATCH_PATH] = {
    id: DISPATCH_PATH,
    filename: DISPATCH_PATH,
    loaded: true,
    exports: { dispatch: fake },
  };
}

function freshList() {
  delete require.cache[LIST_PATH];
  return require('../lib/list.js');
}

const GOOD_HIERARCHY = {
  summary: 'Top complaint: battery drains fast.',
  groups: [
    {
      label: 'Negative',
      sentiment: 'negative',
      items: [
        { id: 'r1', text: 'Battery dies in 2 hours.' },
        { id: 'r2', text: 'Gets hot while charging.' },
      ],
    },
    {
      label: 'Positive',
      sentiment: 'positive',
      items: [{ id: 'r3', text: 'Screen is gorgeous.' }],
    },
  ],
};

const DATA = [
  { id: 'r1', text: 'battery dies in 2 hours, ugh' },
  { id: 'r2', text: 'gets hot while charging' },
  { id: 'r3', text: 'screen is gorgeous!!' },
];

beforeEach(() => {
  delete require.cache[DISPATCH_PATH];
  delete require.cache[LIST_PATH];
});

describe('structureData — strict-schema request per renderMode (AC-MUSE-014)', () => {
  it('requests the structure route with the strict schema selected by renderMode', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: GOOD_HIERARCHY };
    });
    const { structureData } = freshList();
    await structureData({ data: DATA, renderMode: 'executive_summary', prompt: 'Group by sentiment.' });
    assert.equal(seen.length, 1);
    assert.equal(seen[0].route, 'structure');
    assert.equal(seen[0].renderMode, 'executive_summary');
    assert.ok(seen[0].strict === true || /strict/i.test(JSON.stringify(seen[0])));
  });

  it('forwards data + prompt so prompt steers the fill (renderMode selects schema)', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: GOOD_HIERARCHY };
    });
    const { structureData } = freshList();
    await structureData({ data: DATA, renderMode: 'by_object', prompt: 'Surface the top complaint.' });
    assert.deepEqual(seen[0].data, DATA);
    assert.equal(seen[0].prompt, 'Surface the top complaint.');
  });

  it('forwards preset key when given (preset-name-only)', async () => {
    const seen = [];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: GOOD_HIERARCHY };
    });
    const { structureData } = freshList();
    await structureData({ data: DATA, renderMode: 'by_photo', prompt: 'Group.', preset: 'list-fast' });
    assert.equal(seen[0].preset, 'list-fast');
  });

  it('renders summary→groups→items with native rows (keyed ids preserved)', async () => {
    seedDispatchFake(async () => ({ status: 200, body: GOOD_HIERARCHY }));
    const { structureData } = freshList();
    const res = await structureData({ data: DATA, renderMode: 'executive_summary', prompt: 'Group.' });
    assert.equal(typeof res.summary, 'string');
    assert.ok(Array.isArray(res.groups) && res.groups.length === 2);
    assert.equal(res.groups[0].label, 'Negative');
    assert.equal(res.groups[0].sentiment, 'negative');
    assert.deepEqual(
      res.groups[0].items.map((i) => i.id),
      ['r1', 'r2'],
    );
    for (const g of res.groups) {
      for (const item of g.items) {
        assert.equal(typeof item.id, 'string');
        assert.equal(typeof item.text, 'string');
      }
    }
  });

  it('returns route:"structure" + latencyMs alongside the hierarchy', async () => {
    seedDispatchFake(async () => ({ status: 200, body: GOOD_HIERARCHY }));
    const { structureData } = freshList();
    const res = await structureData({ data: DATA, renderMode: 'executive_summary', prompt: 'Group.' });
    assert.equal(res.route, 'structure');
    assert.equal(typeof res.latencyMs, 'number');
  });

  it('throws MuseError schema/schema-violation on violating response (never partial list)', async () => {
    seedDispatchFake(async () => ({ status: 200, body: { groups: [{ label: 'Nope' }] } }));
    const { structureData } = freshList();
    await assert.rejects(
      structureData({ data: DATA, renderMode: 'executive_summary', prompt: 'Group.' }),
      (err) => err && err.name === 'MuseError' && err.code === 'schema' && /schema-violation/.test(err.reason || err.message || ''),
    );
  });

  it('throws MuseError schema/schema-violation on non-list items (never partial list)', async () => {
    seedDispatchFake(async () => ({
      status: 200,
      body: { summary: 's', groups: [{ label: 'L', sentiment: 'negative', items: [{ id: 'r1' }] }] },
    }));
    const { structureData } = freshList();
    await assert.rejects(
      structureData({ data: DATA, renderMode: 'executive_summary', prompt: 'Group.' }),
      (err) => err && err.code === 'schema' && /schema-violation/.test(err.reason || err.message || ''),
    );
  });

  it('surfaces only proxy taxonomy errors from dispatch', async () => {
    const proxyErr = new Error('structure blew up');
    proxyErr.code = 'timeout';
    seedDispatchFake(async () => { throw proxyErr; });
    const { structureData } = freshList();
    await assert.rejects(
      structureData({ data: DATA, renderMode: 'executive_summary', prompt: 'Group.' }),
      (err) => err && ['auth', 'upstream', 'timeout', 'schema'].includes(err.code),
    );
  });
});
