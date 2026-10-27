'use strict';
// TC-TSX-026 (AC-TSX-026) — Batch 2 T3.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T3 `Interface:` line, never to implementation.
// Validator resolution: `../lib/literal-style.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/literal-style.js lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/literal-style.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/literal-style.js lands.
  mod = {
    checkLiteralStyle: () => ({ code: 'STUB', file: 'stub', line: -1, column: -1, nodePath: 'stub' }),
  };
}
const { checkLiteralStyle } = mod;

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
// TC-TSX-026 → AC-TSX-026 — T3 contract:
// `checkLiteralStyle(style: unknown, loc: Loc): null`
// ---------------------------------------------------------------------------
describe('TC-TSX-026 — literal style values compile, never a rejection (AC-TSX-026)', () => {
  it('accepts a literal colour string (null)', () => {
    assert.equal(checkLiteralStyle({ backgroundColor: '#ff0000' }, loc()), null);
  });

  it('accepts a literal dimension number (null)', () => {
    assert.equal(checkLiteralStyle({ marginTop: 8 }, loc()), null);
  });

  it('accepts a literal dimension string (null)', () => {
    assert.equal(checkLiteralStyle({ width: '16dp' }, loc()), null);
  });

  it('accepts another literal string value (null)', () => {
    assert.equal(checkLiteralStyle({ fontWeight: 'bold' }, loc()), null);
  });

  it('returns null for non-object input, never throws', () => {
    assert.equal(checkLiteralStyle(null, loc()), null);
    assert.equal(checkLiteralStyle(undefined, loc()), null);
    assert.equal(checkLiteralStyle('red', loc()), null);
    assert.equal(checkLiteralStyle(42, loc()), null);
  });
});
