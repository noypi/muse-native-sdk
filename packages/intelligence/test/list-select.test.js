'use strict';
// Batch 1 (fr-muse-014) — MuseList row selection: selectRow + retainSelection
// (AC-MUSE-044 / AC-MUSE-045).
// Hermetic: pure selection behavior; dispatch boundary is seeded with a
// counting fake BEFORE list.js loads so "fires no structure call" is asserted
// in-process — no network, no keys. Uses node:test + node:assert.

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

const HIERARCHY_WITH_MEDIA = {
  summary: 'Summary.',
  groups: [
    {
      label: 'Negative',
      sentiment: 'negative',
      items: [
        { id: 'r1', text: 'Battery dies.', photoId: 'p1', maskId: 'm1' },
        { id: 'r2', text: 'Gets hot.' },
      ],
    },
  ],
};

beforeEach(() => {
  delete require.cache[DISPATCH_PATH];
  delete require.cache[LIST_PATH];
});

describe('selectRow — row tap selects entry, navigates without proxy call (AC-MUSE-044)', () => {
  it('selects a known row id and returns route structure with zero structure calls', () => {
    let calls = 0;
    seedDispatchFake(async () => { calls += 1; return { status: 200, body: HIERARCHY }; });
    const { selectRow } = freshList();
    const res = selectRow({ hierarchy: HIERARCHY, rowId: 'r1' });
    assert.equal(res.rowId, 'r1');
    assert.equal(res.route, 'structure');
    assert.equal(res.structureCalls, 0);
    assert.equal(calls, 0);
  });

  it('forwards photoId/maskId when the selected item carries them (ASSUMPTION)', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY_WITH_MEDIA }));
    const { selectRow } = freshList();
    const res = selectRow({ hierarchy: HIERARCHY_WITH_MEDIA, rowId: 'r1' });
    assert.equal(res.rowId, 'r1');
    assert.equal(res.photoId, 'p1');
    assert.equal(res.maskId, 'm1');
    assert.equal(res.structureCalls, 0);
  });

  it('omits photoId/maskId when the selected item carries none (ASSUMPTION)', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { selectRow } = freshList();
    const res = selectRow({ hierarchy: HIERARCHY, rowId: 'r2' });
    assert.equal(res.rowId, 'r2');
    assert.ok(res.photoId === undefined);
    assert.ok(res.maskId === undefined);
  });

  it('throws MuseError unknown-row on unknown id, never a partial selection', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { selectRow } = freshList();
    assert.throws(
      () => selectRow({ hierarchy: HIERARCHY, rowId: 'nope' }),
      (err) => {
        assert.equal(err.name, 'MuseError');
        assert.equal(err.code, 'unknown-row');
        assert.equal(err.message, 'unknown row id: nope');
        return true;
      },
    );
  });

  it('throws unknown-row on empty hierarchy with no partial return', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { selectRow } = freshList();
    const empty = { summary: 'Empty.', groups: [] };
    assert.throws(
      () => selectRow({ hierarchy: empty, rowId: 'r1' }),
      (err) => err && err.code === 'unknown-row',
    );
  });

  it('rejects a malicious row id with unknown-row and no internal leak', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { selectRow } = freshList();
    const evil = '__proto__';
    assert.throws(
      () => selectRow({ hierarchy: HIERARCHY, rowId: evil }),
      (err) => {
        assert.equal(err.code, 'unknown-row');
        assert.ok(!String(err.message).includes('stack'));
        return true;
      },
    );
    assert.equal(HIERARCHY.groups[0].items[0].id, 'r1');
  });
});

describe('retainSelection — preserve selection across renderMode re-request (AC-MUSE-045)', () => {
  it('returns selectedId unchanged when the fresh hierarchy retains the node', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { retainSelection } = freshList();
    const res = retainSelection({ selectedId: 'r2', freshHierarchy: HIERARCHY });
    assert.deepEqual(res, { selectedId: 'r2' });
  });

  it('clears to null with no throw when the node is absent from the fresh hierarchy', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { retainSelection } = freshList();
    const fresh = {
      summary: 'Summary.',
      groups: [
        { label: 'Positive', sentiment: 'positive', items: [{ id: 'r3', text: 'Screen.' }] },
      ],
    };
    let res;
    assert.doesNotThrow(() => { res = retainSelection({ selectedId: 'r1', freshHierarchy: fresh }); });
    assert.deepEqual(res, { selectedId: null });
  });

  it('clears to null with no throw when the fresh hierarchy is null', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { retainSelection } = freshList();
    let res;
    assert.doesNotThrow(() => { res = retainSelection({ selectedId: 'r1', freshHierarchy: null }); });
    assert.deepEqual(res, { selectedId: null });
  });

  it('returns null unchanged when no row is selected (ASSUMPTION)', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { retainSelection } = freshList();
    let res;
    assert.doesNotThrow(() => { res = retainSelection({ selectedId: null, freshHierarchy: HIERARCHY }); });
    assert.deepEqual(res, { selectedId: null });
  });

  it('clears to null with no throw when groups is a non-array truthy value (CWE-755 regression)', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { retainSelection } = freshList();
    for (const groups of [{}, 42]) {
      let res;
      assert.doesNotThrow(() => { res = retainSelection({ selectedId: 'r1', freshHierarchy: { summary: 'S.', groups } }); });
      assert.deepEqual(res, { selectedId: null });
    }
  });

  it('clears to null with no throw when a group items is a non-array truthy value (CWE-755 regression)', () => {
    seedDispatchFake(async () => ({ status: 200, body: HIERARCHY }));
    const { retainSelection } = freshList();
    const fresh = {
      summary: 'S.',
      groups: [{ label: 'Negative', sentiment: 'negative', items: 42 }],
    };
    let res;
    assert.doesNotThrow(() => { res = retainSelection({ selectedId: 'r1', freshHierarchy: fresh }); });
    assert.deepEqual(res, { selectedId: null });
  });

  it('fires no structure call while retaining (ASSUMPTION: retainSelection is pure)', () => {
    let calls = 0;
    seedDispatchFake(async () => { calls += 1; return { status: 200, body: HIERARCHY }; });
    const { retainSelection } = freshList();
    retainSelection({ selectedId: 'r1', freshHierarchy: HIERARCHY });
    retainSelection({ selectedId: 'r1', freshHierarchy: null });
    assert.equal(calls, 0);
  });
});

describe('entry re-export — @muse/intelligence surface (T3, FR-MUSE-014 export dependency)', () => {
  it('re-exports selectRow and retainSelection as functions', () => {
    const entry = require('../index.js');
    assert.equal(typeof entry.selectRow, 'function');
    assert.equal(typeof entry.retainSelection, 'function');
  });

  it('keeps existing exports unchanged', () => {
    const entry = require('../index.js');
    for (const name of ['createTurn', 'createMuseVoice', 'MuseVoice', 'MuseImage', 'createPlatformSpeech']) {
      assert.equal(typeof entry[name], 'function', name);
    }
  });
});
