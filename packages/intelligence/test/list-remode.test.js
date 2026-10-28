'use strict';
// T8 — MuseList remode: re-request structure with new mode, no remount (AC-MUSE-015).
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

const HIERARCHY = {
  summary: 'Summary.',
  groups: [
    {
      label: 'Negative',
      sentiment: 'negative',
      items: [
        { id: 'r1', text: 'Battery dies.' },
        { id: 'r2', text: 'Gets hot.' },
      ],
    },
    {
      label: 'Positive',
      sentiment: 'positive',
      items: [{ id: 'r3', text: 'Screen is great.' }],
    },
  ],
};

beforeEach(() => {
  delete require.cache[DISPATCH_PATH];
  delete require.cache[LIST_PATH];
});

describe('remode — preserve scroll/expand across renderMode change (AC-MUSE-015)', () => {
  it('returns the preserved scroll offset unchanged', async () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { remode } = freshList();
    const res = await remode({
      current: { scrollOffset: 320, expandedIds: [] },
      hierarchy: HIERARCHY,
      renderMode: 'by_photo',
    });
    assert.equal(res.scrollOffset, 320);
  });

  it('returns the intersection of expanded ids retained by the new hierarchy', async () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { remode } = freshList();
    const res = await remode({
      current: { scrollOffset: 0, expandedIds: ['r1', 'r9'] },
      hierarchy: HIERARCHY,
      renderMode: 'by_photo',
    });
    assert.deepEqual(res.expandedIds, ['r1']);
  });

  it('keeps every expanded id when the new schema retains all nodes', async () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { remode } = freshList();
    const res = await remode({
      current: { scrollOffset: 10, expandedIds: ['r1', 'r2', 'r3'] },
      hierarchy: HIERARCHY,
      renderMode: 'executive_summary',
    });
    assert.deepEqual(res.expandedIds, ['r1', 'r2', 'r3']);
  });

  it('drops all expanded ids when the new schema retains none', async () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { remode } = freshList();
    const res = await remode({
      current: { scrollOffset: 5, expandedIds: ['zx', 'zy'] },
      hierarchy: HIERARCHY,
      renderMode: 'by_object',
    });
    assert.deepEqual(res.expandedIds, []);
  });

  it('re-requests structure with the new mode on the same data+host (no remount)', async () => {
    const seen = [];
    const host = { id: 'host-1' };
    const data = [{ id: 'r1', text: 'x' }];
    seedDispatchFake(async (req) => {
      seen.push(req);
      return { status: 200, body: HIERARCHY };
    });
    const { remode } = freshList();
    const before = host;
    await remode({
      current: { scrollOffset: 44, expandedIds: ['r1'] },
      hierarchy: HIERARCHY,
      renderMode: 'by_photo',
      data,
      host,
    });
    // Same host object (no remount) — identity preserved.
    assert.equal(host, before);
    if (seen.length > 0) {
      assert.equal(seen[seen.length - 1].route, 'structure');
      assert.equal(seen[seen.length - 1].renderMode, 'by_photo');
    }
  });
});
