'use strict';
// TC-LAYOUT-019 (AC-LAYOUT-019) — Batch 2 T3.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T3 `Interface:` line, never to implementation.
// Module resolution: `../../../packages/compiler/lib/css-tokens.js` once it
// lands, else an inline fallback stub whose behavior assertions fail, so
// every behavior case below is red until lib/css-tokens.js lands — and the
// harness itself never crashes.
//
// ASSUMPTIONS (pinned in unit-tests/T3.md):
//   - token references use standard CSS `var(--<token-name>)` syntax and
//     resolve through the T4 cascade for the given theme; unknown token names
//     fall to the default tier (never throw), mirroring T4;
//   - the compiler ships the T4 seed dataset (see unit-tests/T4.md), so
//     `var(--t.cascade.probe)` under theme 'probe-dark' resolves to the
//     theme-tier value and the test can cross-check equivalence against
//     `../tokens/store.js` when present.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../../../packages/compiler/lib/css-tokens.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/css-tokens.js lands.
  mod = {
    resolveStylesheet: () => ({}),
    UnsupportedPropertyError: class UnsupportedPropertyError extends Error {},
  };
}
const { resolveStylesheet, UnsupportedPropertyError } = mod;

let store = null;
try {
  store = require('../tokens/store.js');
} catch {
  store = null;
}

// ---------------------------------------------------------------------------
// T3 contract:
// `resolveStylesheet(css: string, theme: string): Record<string, string>`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-019 — stylesheet resolves to the token-equivalent property set; unsupported properties fail source-mapped (AC-LAYOUT-019)', () => {
  it('resolves supported literal declarations to a property set', () => {
    assert.deepEqual(resolveStylesheet('.a { display: flex; color: red; }', 'probe-dark'), {
      display: 'flex',
      color: 'red',
    });
  });

  it('ASSUMPTION: var() token references resolve through the cascade for the given theme', () => {
    const out = resolveStylesheet('.a { color: var(--t.cascade.probe); }', 'probe-dark');
    assert.equal(out.color, 'theme-tier');
  });

  it('resolved var() output deep-equals the equivalent token definition (CSS/token equivalence)', () => {
    assert.ok(store !== null, 'equivalence check needs the T4 store; fails red until T4 lands');
    const expected = store.resolveToken('t.cascade.probe', undefined, 'probe-dark');
    const out = resolveStylesheet('.a { color: var(--t.cascade.probe); }', 'probe-dark');
    assert.equal(out.color, expected.value);
  });

  it('theme changes the resolved value of a theme-tier token reference', () => {
    const dark = resolveStylesheet('.a { color: var(--t.cascade.probe); }', 'probe-dark');
    const light = resolveStylesheet('.a { color: var(--t.cascade.probe); }', 'probe-light');
    assert.notEqual(dark.color, light.color);
  });

  it('unknown var() token names fall to the default tier, never throw', () => {
    const out = resolveStylesheet('.a { color: var(--t.cascade.no-such-token-xyz); }', 'probe-dark');
    assert.equal(typeof out.color, 'string');
  });

  it('returns an empty property set for empty input', () => {
    assert.deepEqual(resolveStylesheet('', 'probe-dark'), {});
  });

  it('throws UnsupportedPropertyError naming the property with line/column', () => {
    assert.throws(
      () => resolveStylesheet('.a {\n  display: flex;\n  float: left;\n}\n', 'probe-dark'),
      (err) => {
        assert.ok(err instanceof UnsupportedPropertyError, 'must throw UnsupportedPropertyError');
        assert.equal(err.property, 'float');
        assert.equal(err.line, 3);
        assert.ok(err.column >= 1, 'column must be 1-based');
        return true;
      },
    );
  });

  it('reports the first offending property on a single line at line 1', () => {
    assert.throws(
      () => resolveStylesheet('.a { animation: spin 1s; }', 'probe-dark'),
      (err) => {
        assert.ok(err instanceof UnsupportedPropertyError, 'must throw UnsupportedPropertyError');
        assert.equal(err.property, 'animation');
        assert.equal(err.line, 1);
        assert.ok(err.column >= 1, 'column must be 1-based');
        return true;
      },
    );
  });

  it('error carries only property/line/column — no internals leak', () => {
    assert.throws(
      () => resolveStylesheet('.a { position: absolute; }', 'probe-dark'),
      (err) => {
        assert.ok(err instanceof UnsupportedPropertyError, 'must throw UnsupportedPropertyError');
        assert.deepEqual(Object.keys(err).sort(), ['column', 'line', 'property']);
        return true;
      },
    );
  });
});
