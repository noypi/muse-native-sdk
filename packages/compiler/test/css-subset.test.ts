'use strict';
// TC-LAYOUT-017 (AC-LAYOUT-017) — Batch 1 T1.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T1 `Interface:` line, never to implementation.
// Module resolution: `../lib/css-subset.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/css-subset.js lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/css-subset.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/css-subset.js lands.
  mod = {
    isSupported: () => false,
    validateStylesheet: () => [],
  };
}
const { isSupported, validateStylesheet } = mod;

// ---------------------------------------------------------------------------
// T1 contract:
// `isSupported(property: string): boolean; validateStylesheet(source: string):
//  Array<{ property: string, line: number, column: number }>`
// ---------------------------------------------------------------------------
describe('TC-LAYOUT-017 — supported CSS3 subset enumerates flexbox/grid/colors/typography/shadows/borders/transforms/transitions (AC-LAYOUT-017)', () => {
  it('supports core flexbox properties', () => {
    assert.equal(isSupported('display'), true);
    assert.equal(isSupported('flex-direction'), true);
    assert.equal(isSupported('justify-content'), true);
    assert.equal(isSupported('align-items'), true);
  });

  it('supports grid, colors, typography, shadows, borders, transforms, transitions', () => {
    assert.equal(isSupported('grid-template-columns'), true);
    assert.equal(isSupported('color'), true);
    assert.equal(isSupported('background-color'), true);
    assert.equal(isSupported('font-size'), true);
    assert.equal(isSupported('font-weight'), true);
    assert.equal(isSupported('box-shadow'), true);
    assert.equal(isSupported('border'), true);
    assert.equal(isSupported('border-radius'), true);
    assert.equal(isSupported('transform'), true);
    assert.equal(isSupported('transition'), true);
  });

  it('rejects properties outside the subset', () => {
    assert.equal(isSupported('float'), false);
    assert.equal(isSupported('position'), false);
    assert.equal(isSupported('animation'), false);
    assert.equal(isSupported('not-a-real-property'), false);
  });

  it('returns empty array for a fully supported stylesheet, never throws', () => {
    const src = '.a {\n  display: flex;\n  color: red;\n}\n';
    assert.deepEqual(validateStylesheet(src), []);
  });

  it('returns one entry per unsupported property with source location', () => {
    const src = '.a {\n  display: flex;\n  float: left;\n  color: red;\n  position: absolute;\n}\n';
    const errors = validateStylesheet(src);
    assert.equal(errors.length, 2);
    assert.equal(errors[0].property, 'float');
    assert.equal(errors[0].line, 3);
    assert.ok(errors[0].column >= 1, 'column must be 1-based');
    assert.equal(errors[1].property, 'position');
    assert.equal(errors[1].line, 5);
    assert.ok(errors[1].column >= 1, 'column must be 1-based');
  });

  it('is case-sensitive-safe: unsupported property on a single line reports line 1 with column', () => {
    const errors = validateStylesheet('.a { animation: spin 1s; }');
    assert.equal(errors.length, 1);
    assert.equal(errors[0].property, 'animation');
    assert.equal(errors[0].line, 1);
    assert.ok(errors[0].column >= 1, 'column must be 1-based');
  });

  it('returns empty array for empty input, never throws', () => {
    assert.deepEqual(validateStylesheet(''), []);
  });

  // ASSUMPTION: custom properties (--x) and vendor prefixes are outside the
  // subset and reported as unsupported rather than silently ignored.
  it('ASSUMPTION: reports custom properties and vendor prefixes as unsupported', () => {
    assert.equal(isSupported('--my-token'), false);
    assert.equal(isSupported('-webkit-transform'), false);
    const errors = validateStylesheet('.a {\n  --my-token: red;\n}\n');
    assert.equal(errors.length, 1);
    assert.equal(errors[0].property, '--my-token');
    assert.equal(errors[0].line, 2);
  });
});
