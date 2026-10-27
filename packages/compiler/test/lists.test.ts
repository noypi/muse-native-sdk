'use strict';
// TC-TSX-014 (AC-TSX-014) + TC-TSX-015 (AC-TSX-015) — Batch 2 (T6–T7 + wiring).
// Hermetic: node:test + node:assert/strict only, inline strings, no network,
// no secrets, no live services.
// Style mirrors test/builtins.test.ts (describe/it + strict assert).
// Binds ONLY to the pinned T6–T7 `Interface:` lines, never to implementation.
// Validator resolution: `../lib/lists.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/lists.js lands — and the harness itself never crashes.
// Equivalence oracle: the real committed `../lib/builtins.js` `compileCFor`
// (Batch 1 T3, committed) is imported read-only and never reimplemented.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let lists = null;
try {
  lists = require('../lib/lists.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/lists.js lands.
  lists = {
    compileMapList: () => null,
    validateMapCallback: () => null,
  };
}
const { compileMapList, validateMapCallback } = lists;

const { compileCFor } = require('../lib/builtins.js');

function loc(overrides = {}) {
  return {
    file: 'Card.tsx',
    line: 21,
    column: 7,
    nodePath: 'Card > list[0]',
    ...overrides,
  };
}

// Local test helper (not a lib surface): compares a `c-for` form
// (each, key) against a canonical `.map()` source string by lowering both
// sides to the same keyed-list output via the T3 oracle + T6 unit.
function forEqualsMap(each, key, mapSource, l) {
  const a = compileCFor(each, key, l);
  const b = compileMapList(mapSource, l);
  try {
    assert.deepEqual(a, b);
    return true;
  } catch {
    return false;
  }
}

// ---------------------------------------------------------------------------
// TC-TSX-014 → AC-TSX-014 — T6 contract:
// `compileMapList(sourceText: string, loc: SlotLocation): { kind: "keyed-list", each: string, key: string } | BuiltinDiagnostic`
// ---------------------------------------------------------------------------
describe('TC-TSX-014 — canonical .map() compiles to a keyed list (AC-TSX-014)', () => {
  it('compiles items.map((item) => <Row key={item.id} />) to a keyed-list node', () => {
    assert.deepEqual(
      compileMapList('items.map((item) => <Row key={item.id} />)', loc()),
      { kind: 'keyed-list', each: 'items', key: 'item.id' },
    );
  });

  it('fails a mapped element without key with a diagnostic naming key', () => {
    const l = loc();
    const d = compileMapList('items.map((item) => <Row />)', l);
    assert.notEqual(d, null, 'missing key must return a diagnostic');
    assert.ok(d.form.includes('key'), 'diagnostic form must name `key`');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('fails a mapped element with an empty key with a diagnostic naming key', () => {
    const d = compileMapList('items.map((item) => <Row key={} />)', loc());
    assert.notEqual(d, null, 'empty key must return a diagnostic');
    assert.ok(d.form.includes('key'), 'diagnostic form must name `key`');
  });

  it('compiles a paired canonical element to the same keyed-list node', () => {
    assert.deepEqual(
      compileMapList('items.map((item) => <Row key={item.id}></Row>)', loc()),
      { kind: 'keyed-list', each: 'items', key: 'item.id' },
    );
  });
});

// ---------------------------------------------------------------------------
// TC-TSX-015 → AC-TSX-015 — T7 contract:
// `validateMapCallback(sourceText: string, loc: SlotLocation): BuiltinDiagnostic | null`
// ---------------------------------------------------------------------------
describe('TC-TSX-015 — block-body / non-single-JSX callbacks fail; c-for equivalence (AC-TSX-015)', () => {
  it('rejects a block-body callback with a non-null diagnostic', () => {
    const l = loc();
    const d = validateMapCallback(
      'items.map((item) => { return <Row key={item.id} />; })',
      l,
    );
    assert.notEqual(d, null, 'block body must return a diagnostic');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
  });

  it('rejects a non-single-JSX body (identifier expression) with a non-null diagnostic', () => {
    const d = validateMapCallback('items.map((item) => item.id)', loc());
    assert.notEqual(d, null, 'non-JSX body must return a diagnostic');
  });

  it('accepts the canonical single-JSX callback (null)', () => {
    assert.equal(
      validateMapCallback('items.map((item) => <Row key={item.id} />)', loc()),
      null,
    );
  });

  it('equivalent c-for form compiles to the same keyed-list output (true)', () => {
    assert.equal(
      forEqualsMap(
        'items',
        'item.id',
        'items.map((item) => <Row key={item.id} />)',
        loc(),
      ),
      true,
    );
  });

  it('mismatched key does not compare equal (false)', () => {
    assert.equal(
      forEqualsMap(
        'items',
        'item.other',
        'items.map((item) => <Row key={item.id} />)',
        loc(),
      ),
      false,
    );
  });

  it('rejects a mismatched open/close tag pair with a non-null diagnostic', () => {
    const d = validateMapCallback('items.map((i) => <Row>x</Div>)', loc());
    assert.notEqual(d, null, 'mismatched tags must return a diagnostic');
  });

  it('accepts the paired canonical single-JSX callback (null)', () => {
    assert.equal(
      validateMapCallback(
        'items.map((item) => <Row key={item.id}></Row>)',
        loc(),
      ),
      null,
    );
  });
});
