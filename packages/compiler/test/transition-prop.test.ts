'use strict';
// TC-TSX-029 (AC-TSX-029) — Batch 3 T6.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T6 `Interface:` line, never to implementation.
// Validator resolution: `../lib/motion.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/motion.js lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/motion.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/motion.js lands.
  mod = {
    checkTransitionProp: () => ({ code: 'STUB' }),
  };
}
const { checkTransitionProp } = mod;

function loc(overrides = {}) {
  return {
    file: 'Login.tsx',
    line: 12,
    column: 5,
    nodePath: 'Login > View[0]',
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// TC-TSX-029 → AC-TSX-029 — T6 contract:
// `checkTransitionProp(prop: string, value: unknown, loc: Loc): Diagnostic|null`
// ---------------------------------------------------------------------------
describe('TC-TSX-029 — transition token maps accepted, literal curve/duration rejected (AC-TSX-029)', () => {
  it("accepts a token-string value (null) — {'opacity':'fade'}", () => {
    assert.equal(checkTransitionProp('opacity', 'fade', loc()), null);
  });

  it('rejects a literal duration object naming the property', () => {
    const l = loc();
    const d = checkTransitionProp('opacity', { duration: 300 }, l);
    assert.notEqual(d, null, 'literal duration must return a diagnostic');
    assert.equal(d.code, 'TSX_TRANSITION_LITERAL');
    assert.equal(d.property, 'opacity');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
    assert.equal(d.nodePath, l.nodePath);
    assert.equal(d.severity, 'error');
  });

  it('rejects a literal curve string naming the property', () => {
    const l = loc();
    const d = checkTransitionProp('transform', 'cubic-bezier(0.2,0,0,1)', l);
    assert.notEqual(d, null, 'literal curve must return a diagnostic');
    assert.equal(d.code, 'TSX_TRANSITION_LITERAL');
    assert.equal(d.property, 'transform');
    assert.equal(d.severity, 'error');
  });

  it('rejects a numeric literal duration naming the property', () => {
    const l = loc();
    const d = checkTransitionProp('opacity', 300, l);
    assert.notEqual(d, null, 'numeric duration must return a diagnostic');
    assert.equal(d.code, 'TSX_TRANSITION_LITERAL');
    assert.equal(d.property, 'opacity');
    assert.equal(d.severity, 'error');
  });

  it('never throws on unknown input shapes', () => {
    assert.doesNotThrow(() => checkTransitionProp('opacity', undefined, loc()));
    assert.doesNotThrow(() => checkTransitionProp('opacity', null, loc()));
  });

  it('returns null for an absent (undefined) prop value — not a literal', () => {
    assert.equal(checkTransitionProp('opacity', undefined, loc()), null);
  });

  it('rejects a boolean value as a non-token-string literal naming the property', () => {
    const l = loc();
    const d = checkTransitionProp('opacity', true, l);
    assert.notEqual(d, null, 'boolean value must return a diagnostic');
    assert.equal(d.code, 'TSX_TRANSITION_LITERAL');
    assert.equal(d.property, 'opacity');
    assert.equal(d.severity, 'error');
  });
});
