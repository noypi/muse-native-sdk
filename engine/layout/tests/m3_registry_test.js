'use strict';
// TC-LAYOUT-024 (AC-LAYOUT-024) — Batch 3 T8.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T8 `Interface:` line, never to implementation.
// Module resolution: `../m3_registry.js` once it lands, else an inline
// fallback stub that throws a GENERIC error (wrong name, no `component`
// field) so every behavior case below is red until the registry lands —
// and the harness itself never crashes.
//
// ASSUMPTION (tier-3 list, pinned in unit-tests/T8.md): the 14 tier-3
// composites are exactly the SPEC-M3-NATIVE tier-3 row — TopAppBar,
// NavigationBar, NavigationRail, NavigationDrawer, SegmentedButton, Dialog,
// BottomSheet, Menu, SnackbarHost, SearchBar, Carousel, BottomAppBar,
// ButtonGroup, SplitButton — each `kind: 'composite'` with a TSX `ref`.
// ASSUMPTION (native sample): Button, Card, Chip, Text, FAB, Slider, Badge
// are tier-1/2 `kind: 'native'` entries. The full matrix is published by T11;
// these samples pin the contract until then.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../m3_registry.js');
} catch {
  // Fallback stub: present so the harness loads; throws a generic error so
  // every behavior assertion below fails (red) until ../m3_registry.js lands.
  mod = {
    getPrimitive: () => {
      throw new Error('m3 registry not implemented');
    },
  };
}
const { getPrimitive } = mod;

const TIER3_COMPOSITES = [
  'TopAppBar',
  'NavigationBar',
  'NavigationRail',
  'NavigationDrawer',
  'SegmentedButton',
  'Dialog',
  'BottomSheet',
  'Menu',
  'SnackbarHost',
  'SearchBar',
  'Carousel',
  'BottomAppBar',
  'ButtonGroup',
  'SplitButton',
];

const NATIVE_SAMPLE = ['Button', 'Card', 'Chip', 'Text', 'FAB', 'Slider', 'Badge'];

const TOKEN_REF_RE = /^[a-z][a-z0-9-]*(\.[a-z0-9-]+)+$/;

function assertEntryShape(entry) {
  assert.equal(typeof entry, 'object');
  assert.equal(typeof entry.primitive, 'string');
  assert.ok(entry.primitive.length > 0, 'primitive must be non-empty');
  assert.ok(Array.isArray(entry.tokens), 'tokens must be an array');
  assert.ok(entry.tokens.length > 0, 'tokens must be non-empty');
  for (const t of entry.tokens) {
    assert.equal(typeof t, 'string');
    assert.match(t, TOKEN_REF_RE, `token reference must be a dotted path, got: ${t}`);
  }
  assert.ok(entry.kind === 'native' || entry.kind === 'composite');
  assert.equal(typeof entry.ref, 'string');
  assert.ok(entry.ref.length > 0, 'ref must be non-empty');
}

// ---------------------------------------------------------------------------
// T8 contract:
// `getPrimitive(component: string): { primitive: string, tokens: string[],
//  kind: 'native' | 'composite', ref: string }`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-024 — registry resolves every matrix component (AC-LAYOUT-024)', () => {
  it('resolves a tier-1/2 sample entry as kind native with tokens and ref', () => {
    const entry = getPrimitive('Button');
    assertEntryShape(entry);
    assert.equal(entry.kind, 'native');
  });

  for (const name of NATIVE_SAMPLE) {
    it(`resolves native sample '${name}' as kind 'native'`, () => {
      const entry = getPrimitive(name);
      assertEntryShape(entry);
      assert.equal(entry.kind, 'native');
    });
  }

  it('marks exactly the 14 tier-3 composites as kind composite', () => {
    assert.equal(TIER3_COMPOSITES.length, 14);
    for (const name of TIER3_COMPOSITES) {
      const entry = getPrimitive(name);
      assertEntryShape(entry);
      assert.equal(entry.kind, 'composite', `${name} must be composite per CONFLICT-M3-001`);
    }
  });

  for (const name of TIER3_COMPOSITES) {
    it(`tier-3 composite '${name}' links a TSX ref`, () => {
      const entry = getPrimitive(name);
      assert.ok(
        entry.ref.endsWith('.tsx'),
        `composite ref must be a TSX path, got: ${entry.ref}`
      );
    });
  }

  it('every entry is driven only by tokens: tokens are references, never literals', () => {
    for (const name of [...NATIVE_SAMPLE, ...TIER3_COMPOSITES]) {
      const entry = getPrimitive(name);
      for (const t of entry.tokens) {
        assert.match(t, TOKEN_REF_RE, `${name}: token must be a reference, got: ${t}`);
      }
    }
  });

  it('is deterministic: repeated calls deep-equal', () => {
    assert.deepEqual(getPrimitive('Button'), getPrimitive('Button'));
    assert.deepEqual(getPrimitive('Dialog'), getPrimitive('Dialog'));
  });

  it('throws UnknownComponentError carrying { component } for unknown names', () => {
    assert.throws(
      () => getPrimitive('NoSuchComponent'),
      (err) => err.name === 'UnknownComponentError' && err.component === 'NoSuchComponent'
    );
  });

  it('throws UnknownComponentError for an empty name', () => {
    assert.throws(
      () => getPrimitive(''),
      (err) => err.name === 'UnknownComponentError' && err.component === ''
    );
  });

  it('does not leak prototype fields: __proto__ throws instead of resolving', () => {
    assert.throws(
      () => getPrimitive('__proto__'),
      (err) => err.name === 'UnknownComponentError'
    );
  });

  it('throws UnknownComponentError for non-string input (ASSUMPTION: treated as unknown)', () => {
    for (const bad of [123, null, undefined]) {
      assert.throws(
        () => getPrimitive(bad),
        (err) => err.name === 'UnknownComponentError' && err.component === bad
      );
    }
  });
});
