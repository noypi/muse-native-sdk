'use strict';
// T3 — Declared config → minimal invalidation map (layout/resources/fonts/theme, never full rebuild).
// Plan Interface (verbatim):
// `getInvalidation(change: string): { targets: Array<'layout'|'resources'|'fonts'|'theme'> }` — throws `CONFIG_UNKNOWN_CHANGE` for any string outside the frozen 13; return order is canonical `['layout','resources','fonts','theme']` filtered.
// Spec refs: FR-LIFECYCLE-006, AC-LIFECYCLE-014 (TC-LIFECYCLE-014 hermetic mirror).
// Hermetic: node:test + node:assert/strict only; no network/fs/secrets/live services.
// ASSUMPTIONs: (a) non-string inputs (undefined/null/number/object) also throw
// CONFIG_UNKNOWN_CHANGE (plan pins strings; loud failure is the defensible
// behavior); (b) the returned `targets` array is a fresh copy per call
// (mutating it does not affect later calls); (c) exact per-change table beyond
// the three acceptance spot-checks follows the plan Detail minimal map but the
// test pins shape (non-empty, valid tokens, canonical order, no full rebuild)
// rather than the full table.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const manager = require('../src/config_manager.js');

const ALL_13 = [
  'locale',
  'layoutDirection',
  'density',
  'fontScale',
  'orientation',
  'screenSize',
  'smallestScreenSize',
  'uiMode',
  'colorMode',
  'keyboard',
  'keyboardHidden',
  'navigation',
  'screenLayout',
];

const CANONICAL = ['layout', 'resources', 'fonts', 'theme'];
const VALID = new Set(CANONICAL);

function canonicalSorted(targets) {
  return [...targets].sort((a, b) => CANONICAL.indexOf(a) - CANONICAL.indexOf(b));
}

function assertUnknownChange(fn) {
  assert.throws(
    fn,
    (err) => err && err.code === 'CONFIG_UNKNOWN_CHANGE'
  );
}

describe('TC-LIFECYCLE-014 invalidation map — module shape', () => {
  it('exports getInvalidation as a function', () => {
    assert.equal(typeof manager.getInvalidation, 'function');
  });
});

describe('TC-LIFECYCLE-014 invalidation map — all 13 changes map minimally', () => {
  it('every frozen change returns a non-empty target set with no full-rebuild token', () => {
    for (const change of ALL_13) {
      const result = manager.getInvalidation(change);
      assert.ok(Array.isArray(result.targets), `${change} must return a targets array`);
      assert.ok(result.targets.length > 0, `${change} must invalidate something`);
      for (const target of result.targets) {
        assert.ok(VALID.has(target), `${change}: target ${JSON.stringify(target)} must be one of the four tokens`);
      }
      assert.ok(
        !result.targets.some((t) => /rebuild/i.test(String(t))),
        `${change} must never map to a full rebuild`
      );
    }
  });

  it('targets are in canonical order (layout, resources, fonts, theme) filtered', () => {
    for (const change of ALL_13) {
      const { targets } = manager.getInvalidation(change);
      assert.deepEqual(targets, canonicalSorted(targets), `${change} targets must be canonical-ordered`);
      assert.equal(new Set(targets).size, targets.length, `${change} targets must not repeat`);
    }
  });

  it('fontScale includes fonts, uiMode includes theme, orientation includes layout', () => {
    assert.ok(manager.getInvalidation('fontScale').targets.includes('fonts'));
    assert.ok(manager.getInvalidation('uiMode').targets.includes('theme'));
    assert.ok(manager.getInvalidation('orientation').targets.includes('layout'));
  });

  it('density resolves fonts and locale resolves resources (minimal-map spot checks)', () => {
    assert.ok(manager.getInvalidation('density').targets.includes('fonts'));
    assert.ok(manager.getInvalidation('locale').targets.includes('resources'));
  });
});

describe('TC-LIFECYCLE-014 invalidation map — unknown changes throw', () => {
  it('unknown strings throw CONFIG_UNKNOWN_CHANGE', () => {
    for (const change of ['', 'rotation', 'CONFIG_UNKNOWN', 'orientation '] ) {
      assertUnknownChange(() => manager.getInvalidation(change));
    }
  });

  it('prototype-pollution and oversize strings throw CONFIG_UNKNOWN_CHANGE', () => {
    // Security: untrusted names must fail loudly, never resolve to a target set.
    for (const change of ['__proto__', 'constructor', 'prototype', 'a'.repeat(1000)]) {
      assertUnknownChange(() => manager.getInvalidation(change));
    }
  });

  it('non-string inputs throw CONFIG_UNKNOWN_CHANGE (ASSUMPTION a)', () => {
    for (const change of [undefined, null, 42, {}, []]) {
      assertUnknownChange(() => manager.getInvalidation(change));
    }
  });
});

describe('TC-LIFECYCLE-014 invalidation map — result isolation', () => {
  it('returned targets array is a fresh copy per call (ASSUMPTION b)', () => {
    const first = manager.getInvalidation('orientation');
    first.targets.push('theme');
    first.targets.length = 0;
    const second = manager.getInvalidation('orientation');
    assert.ok(second.targets.length > 0);
    assert.ok(second.targets.includes('layout'));
  });
});
