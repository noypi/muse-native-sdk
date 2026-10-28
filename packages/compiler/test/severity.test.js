'use strict';
// TC-WORKFLOW-013 (T4: FR-WORKFLOW-005 AC-WORKFLOW-013).
// T4 scope: configurable diagnostic severity with library/app defaults
// (incl CAPO0012 advisory shape). Binds ONLY to the pinned T4 `Interface:`
// lines, never to implementation internals.
// Hermetic: node:test + node:assert/strict only. Pure in-process calls to
// `resolveSeverity` / `checkLiteralAgainstTokens` with inline fixtures; no
// fs reads, no network, no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T4a: `resolveSeverity(code, configured, packageType)` is exported from
//   `packages/compiler/lib/diagnostics.js` and is pure (no I/O).
// - A-T4b: the only invalid-`configured` rejection is a `ConfigError`-shaped
//   throw (`name === 'ConfigError'`, `key === code`, `expected` naming
//   `error|warn|off`, message `Error: <code> in <config>: expected ...`);
//   `checkLiteralAgainstTokens` never throws and keeps its advisory shape.
// - A-T4c: file label is `.js` per the plan batch table (spec Verification
//   column names `.ts`); run via the plan's exact command
//   `node --test packages/compiler/test/severity.test.js`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const mod = require('../lib/diagnostics.js');
const { resolveSeverity, checkLiteralAgainstTokens } = mod;

function loc(overrides = {}) {
  return {
    file: 'Login.tsx',
    line: 12,
    column: 5,
    nodePath: 'Login > View[0]',
    ...overrides,
  };
}

describe('T4 resolveSeverity — explicit configured value wins (AC-013)', () => {
  it('exports resolveSeverity as a function', () => {
    assert.equal(typeof resolveSeverity, 'function', 'diagnostics.js must export resolveSeverity()');
  });

  it('explicit error wins for both package types', () => {
    assert.equal(resolveSeverity('CAPO0012', 'error', 'library'), 'error');
    assert.equal(resolveSeverity('CAPO0012', 'error', 'application'), 'error');
  });

  it('explicit warn wins for both package types', () => {
    assert.equal(resolveSeverity('CAPO0012', 'warn', 'library'), 'warn');
    assert.equal(resolveSeverity('CAPO0012', 'warn', 'application'), 'warn');
  });

  it('explicit off silences for both package types', () => {
    assert.equal(resolveSeverity('CAPO0012', 'off', 'library'), 'off');
    assert.equal(resolveSeverity('CAPO0012', 'off', 'application'), 'off');
  });

  it('invalid configured value throws ConfigError naming code + error|warn|off', () => {
    assert.throws(
      () => resolveSeverity('CAPO0012', 'scream', 'application'),
      (err) => {
        assert.equal(err.name, 'ConfigError');
        assert.equal(err.key, 'CAPO0012');
        assert.match(String(err.expected), /error\|warn\|off/);
        assert.match(String(err.message), /Error: CAPO0012/);
        return true;
      },
    );
  });
});

describe('T4 resolveSeverity — library/app defaults (AC-013)', () => {
  it('defaults to error when packageType is library', () => {
    assert.equal(resolveSeverity('CAPO0012', undefined, 'library'), 'error');
  });

  it('defaults to warn when packageType is application', () => {
    assert.equal(resolveSeverity('CAPO0012', undefined, 'application'), 'warn');
  });

  it('defaults to warn when packageType is missing/empty', () => {
    assert.equal(resolveSeverity('CAPO0012', undefined, undefined), 'warn');
    assert.equal(resolveSeverity('CAPO0012', undefined, ''), 'warn');
  });

  it('never throws on an unknown code — returns the default', () => {
    assert.equal(resolveSeverity('CAPO9999', undefined, 'application'), 'warn');
    assert.equal(resolveSeverity('CAPO9999', undefined, 'library'), 'error');
    assert.equal(resolveSeverity('CAPO9999', 'off', 'application'), 'off');
  });
});

describe('T4 checkLiteralAgainstTokens — unchanged advisory shape (AC-013)', () => {
  const KNOWN = [
    { token: 'primary', value: '#6750A4' },
    { token: 'on-surface', value: '#1D1B20' },
  ];

  it('returns null for a non-matching literal, never throws', () => {
    assert.equal(checkLiteralAgainstTokens('#ff0000', loc(), KNOWN), null);
  });

  it('match returns CAPO0012 advisory shape with exitCode 0', () => {
    const l = loc();
    const d = checkLiteralAgainstTokens('#6750A4', l, KNOWN);
    assert.notEqual(d, null);
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

  it('returns null against an empty known-token list, never throws', () => {
    assert.equal(checkLiteralAgainstTokens('#6750A4', loc(), []), null);
  });

  it('effective severity comes from resolveSeverity, not the advisory shape', () => {
    const d = checkLiteralAgainstTokens('#6750A4', loc(), KNOWN);
    assert.notEqual(d, null);
    assert.equal(d.severity, 'advisory');
    assert.equal(resolveSeverity(d.code, 'off', 'library'), 'off');
    assert.equal(resolveSeverity(d.code, undefined, 'library'), 'error');
    assert.equal(resolveSeverity(d.code, undefined, 'application'), 'warn');
  });
});
