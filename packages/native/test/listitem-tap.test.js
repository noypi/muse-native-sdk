'use strict';
// BUG-MUSE-004 tap path — ListItem tap event contract (TC-MUSE-043/044 tap half).
// support.md (workflow.local/runs/muse-004): programmatic selectRow is green
// but no tap event ever reaches it — manifest declares no ListItem event,
// the factory wires no event, onSelectTap throws for ListItem.
// These tests pin the JS layers of the tap chain; every one is RED until the
// coder plumbs the event (expect TypeError / missing-prop failures, never
// silent passes).
//
// Hermetic: node:test + node:assert/strict only. Reads
// muse.manifest.json from disk, imports ListItem/onSelectTap/selectRow
// through the package public entry (../index.js). selectRow is pure
// (in-memory hierarchy fixtures); no network, no secrets, no services,
// no device.
//
// ASSUMPTION DISPATCH-1 (ListItem tap call shape): the plan/support.md name
// the dispatch branch but no signature. The most defensible shape reuses the
// existing entry point: `onSelectTap('ListItem', { rowId, hierarchy,
// enabled? })` returns the selectRow result for the tapped row. If the coder
// ships a dedicated `onListItemTap({ rowId, hierarchy })` instead, update
// these call sites — not the expected behavior (tap known id selects +
// navigates with zero structure calls; tap unknown id throws named
// unknown-row; disabled tap fires nothing).
// ASSUMPTION DISABLED-1: mirroring Checkbox/Radio disabled semantics,
// a ListItem tap with `enabled: false` returns state unchanged and never
// calls selectRow (no selection, no throw).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const path = require('node:path');

const entry = require('../index.js');
const { components } = entry;

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

function manifestProps(name) {
  const manifest = JSON.parse(
    readFileSync(path.join(__dirname, '..', 'muse.manifest.json'), 'utf8'),
  );
  const comp = manifest.components.find((c) => c.name === name);
  assert.ok(comp, `manifest must contain a ${name} entry`);
  return comp.props ?? {};
}

// ---------------------------------------------------------------------------
// TAP-1 — manifest ListItem tap event prop (AC-MUSE-044 tap reachability)
// ---------------------------------------------------------------------------

describe('TAP-1: ListItem manifest declares a tap event (AC-MUSE-044)', () => {
  it('exposes onPress {type:event} mirroring Button (pressable taps use event)', () => {
    const props = manifestProps('ListItem');
    assert.deepEqual(
      props.onPress,
      { type: 'event' },
      'ListItem must declare onPress:{type:"event"} like Button/IconButton/Fab',
    );
  });

  it('keeps the existing ListItem props untouched (additive event only)', () => {
    const props = manifestProps('ListItem');
    assert.equal(props.headline.type, 'string');
    assert.equal(props.headline.required, true);
    assert.ok('supporting' in props || 'icon' in props || 'enabled' in props);
  });
});

// ---------------------------------------------------------------------------
// TAP-2 — factory carries the event; dispatch routes the tap to selectRow
// ---------------------------------------------------------------------------

describe('TAP-2: ListItem factory + onSelectTap dispatch (AC-MUSE-044)', () => {
  it('factory carries onPress through on the ListItem props', () => {
    assert.equal(typeof components.ListItem, 'function', 'ListItem must be exported');
    let fired = 0;
    const node = components.ListItem({
      headline: 'Battery',
      onPress: () => { fired += 1; },
    });
    assert.equal(node.type, 'ListItem');
    assert.equal(typeof node.props.onPress, 'function', 'factory must carry the tap event');
    node.props.onPress();
    assert.equal(fired, 1, 'carried handler must fire exactly once per tap');
  });

  it('tap on a known row selects it and navigates with zero structure calls', () => {
    assert.equal(typeof entry.onSelectTap, 'function', 'onSelectTap must be exported');
    const res = entry.onSelectTap('ListItem', { rowId: 'r1', hierarchy: HIERARCHY });
    assert.equal(res.rowId, 'r1');
    assert.equal(res.route, 'structure');
    assert.equal(res.structureCalls, 0);
  });

  it('tap result agrees with a direct selectRow call on the same row (parity)', () => {
    const viaTap = entry.onSelectTap('ListItem', { rowId: 'r2', hierarchy: HIERARCHY });
    const direct = entry.selectRow({ hierarchy: HIERARCHY, rowId: 'r2' });
    assert.deepEqual(viaTap, direct, 'tap dispatch must deliver exactly what selectRow returns');
  });

  it('tap on an unknown row throws the named unknown-row error, never a silent no-op', () => {
    assert.throws(
      () => entry.onSelectTap('ListItem', { rowId: 'nope', hierarchy: HIERARCHY }),
      (err) => {
        assert.equal(err.name, 'MuseError');
        assert.equal(err.code, 'unknown-row');
        assert.equal(err.message, 'unknown row id: nope');
        return true;
      },
    );
  });

  it('tap on an empty hierarchy throws unknown-row with no partial selection', () => {
    assert.throws(
      () => entry.onSelectTap('ListItem', { rowId: 'r1', hierarchy: { summary: 'Empty.', groups: [] } }),
      (err) => err && err.code === 'unknown-row',
    );
  });

  it('disabled ListItem tap fires nothing and changes nothing (ASSUMPTION DISABLED-1)', () => {
    const prev = { rowId: 'r1', hierarchy: HIERARCHY, enabled: false };
    const snapshot = { ...prev };
    const res = entry.onSelectTap('ListItem', prev);
    assert.deepEqual({ ...prev }, snapshot, 'disabled tap must not mutate its input');
    assert.ok(res.rowId === undefined, 'disabled tap must not produce a selection');
  });

  it('tap rejects a malicious row id with unknown-row and no internal leak', () => {
    assert.throws(
      () => entry.onSelectTap('ListItem', { rowId: '__proto__', hierarchy: HIERARCHY }),
      (err) => {
        assert.equal(err.code, 'unknown-row');
        assert.ok(!String(err.message).includes('stack'));
        return true;
      },
    );
    assert.equal(HIERARCHY.groups[0].items[0].id, 'r1');
  });
});
