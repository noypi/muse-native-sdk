'use strict';
// TC-TSX-027 (AC-TSX-027) — Batch 2 T4.
// Hermetic: node:test + node:assert/strict only, inline fixtures, no network,
// no secrets, no fs reads, no live services.
// Binds ONLY to the pinned T4 `Interface:` line, never to implementation.
// Validator resolution: `../lib/diagnostics.js` once it lands, else an inline
// fallback stub whose behavior assertions fail, so every behavior case below
// is red until lib/diagnostics.js lands — and the harness itself never crashes.
// Known-token injection (plan A4): the test passes `knownTokenValues`
// explicitly — caller-supplied list, no gate changes, no fs reads in tests.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let mod = null;
try {
  mod = require('../lib/diagnostics.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/diagnostics.js lands.
  mod = {
    checkLiteralAgainstTokens: () => ({ code: 'STUB' }),
  };
}
const { checkLiteralAgainstTokens } = mod;

function loc(overrides = {}) {
  return {
    file: 'Login.tsx',
    line: 12,
    column: 5,
    nodePath: 'Login > View[0]',
    ...overrides,
  };
}

const KNOWN = [
  { token: 'primary', value: '#6750A4' },
  { token: 'on-surface', value: '#1D1B20' },
];

// ---------------------------------------------------------------------------
// TC-TSX-027 → AC-TSX-027 — T4 contract:
// `checkLiteralAgainstTokens(literal: string|number, loc: Loc, knownTokenValues: Array<{token: string, value: string|number}>): Diagnostic|null`
// ---------------------------------------------------------------------------
describe('TC-TSX-027 — advisory CAPO0012 on token-value duplication, exit 0 (AC-TSX-027)', () => {
  it('returns null for a non-matching literal, never throws', () => {
    assert.equal(checkLiteralAgainstTokens('#ff0000', loc(), KNOWN), null);
  });

  it('returns null for a non-matching numeric literal', () => {
    assert.equal(checkLiteralAgainstTokens(999, loc(), KNOWN), null);
  });

  it('emits CAPO0012 naming literal + location + token with exit 0 on match', () => {
    const l = loc();
    const d = checkLiteralAgainstTokens('#6750A4', l, KNOWN);
    assert.notEqual(d, null, 'matching literal must return a diagnostic');
    assert.equal(d.code, 'CAPO0012');
    assert.equal(d.literal, '#6750A4');
    assert.equal(d.file, l.file);
    assert.equal(d.line, l.line);
    assert.equal(d.column, l.column);
    assert.equal(d.nodePath, l.nodePath);
    assert.equal(d.token, 'primary');
    assert.equal(d.severity, 'advisory');
    assert.equal(d.exitCode, 0);
  });

  it('matches a numeric token value with exit 0', () => {
    const known = [{ token: 'elevation', value: 8 }];
    const l = loc();
    const d = checkLiteralAgainstTokens(8, l, known);
    assert.notEqual(d, null, 'matching numeric literal must return a diagnostic');
    assert.equal(d.code, 'CAPO0012');
    assert.equal(d.token, 'elevation');
    assert.equal(d.severity, 'advisory');
    assert.equal(d.exitCode, 0);
  });

  it('returns null against an empty known-token list, never throws', () => {
    assert.equal(checkLiteralAgainstTokens('#6750A4', loc(), []), null);
  });
});
