'use strict';
// T11 — Minified-release accept: zero R8 warnings + matrix green + mapping present
// (NFR-BUILD-007 / AC-BUILD-015; composes T9 + T10 plus warning scan — all
// inputs below are synthetic in-memory doubles, no T9/T10 module imports).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./warningGate.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   releaseAccept(warnings: {kind:string, count:number}[], matrix: {suite:string, passed:boolean, signal?:string}[], mapping: {path:string, bytes:number}): {ok:boolean, reasons:string[]}
// composes T9 + T10 plus warning scan; any missing-class/invalid-keep warning
// with count > 0, any matrix failure, or absent mapping fails with named reasons.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./warningGate.js');
}

function greenArgs() {
  return {
    warnings: [],
    matrix: [{ suite: 'minified-smoke', passed: true }],
    mapping: { path: 'dist/mapping.txt', bytes: 128 },
  };
}

describe('T11 — contract shape (AC-BUILD-015)', () => {
  it('returns { ok: boolean, reasons: string[] } for a fully green release', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, matrix, mapping } = greenArgs();
    const result = releaseAccept(warnings, matrix, mapping);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.reasons));
    assert.equal(result.ok, true);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T11 — green release passes (AC-BUILD-015)', () => {
  it('zero warnings + green matrix + present mapping passes', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, matrix, mapping } = greenArgs();
    assert.equal(releaseAccept(warnings, matrix, mapping).ok, true);
  });

  it('zero-count warning entries pass (boundary: count > 0 fails)', () => {
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    const result = releaseAccept(
      [
        { kind: 'missing-class', count: 0 },
        { kind: 'invalid-keep', count: 0 },
      ],
      matrix,
      mapping
    );
    assert.equal(result.ok, true);
  });

  it('unknown warning kinds with count > 0 do not fail the gate', () => {
    // ASSUMPTION: only missing-class / invalid-keep kinds are release
    // blockers; informational kinds (e.g. notes) are ignored.
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    const result = releaseAccept(
      [{ kind: 'note-optimization', count: 3 }],
      matrix,
      mapping
    );
    assert.equal(result.ok, true);
  });
});

describe('T11 — each failing leg fails naming that leg', () => {
  it('missing-class warning with count > 0 fails naming the warning leg', () => {
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    const result = releaseAccept(
      [{ kind: 'missing-class', count: 2 }],
      matrix,
      mapping
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => r.includes('missing-class')),
      `reasons must name the warning, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('invalid-keep warning with count > 0 fails naming the warning leg', () => {
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    const result = releaseAccept(
      [{ kind: 'invalid-keep', count: 1 }],
      matrix,
      mapping
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => r.includes('invalid-keep')));
  });

  it('matrix failure fails naming the matrix leg', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, mapping } = greenArgs();
    const result = releaseAccept(
      warnings,
      [{ suite: 'minified-smoke', passed: false }],
      mapping
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => r.includes('minified-smoke') || /matrix/i.test(r)),
      `reasons must name the matrix leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('matrix shrink signal fails naming the matrix leg', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, mapping } = greenArgs();
    const result = releaseAccept(
      warnings,
      [
        {
          suite: 'minified-smoke',
          passed: true,
          signal: 'NoSuchMethodError: com.example.Foo.bar',
        },
      ],
      mapping
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });

  it('absent mapping (wrong path) fails naming the mapping leg', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, matrix } = greenArgs();
    const result = releaseAccept(warnings, matrix, {
      path: 'dist/other.txt',
      bytes: 128,
    });
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => /mapping/i.test(r)),
      `reasons must name the mapping leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('empty mapping (zero bytes) fails naming the mapping leg', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, matrix } = greenArgs();
    const result = releaseAccept(warnings, matrix, {
      path: 'dist/mapping.txt',
      bytes: 0,
    });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.some((r) => /mapping/i.test(r)));
  });

  it('combined failures name every failing leg', () => {
    const { releaseAccept } = loadImpl();
    const result = releaseAccept(
      [{ kind: 'missing-class', count: 1 }],
      [{ suite: 'minified-smoke', passed: false }],
      { path: 'dist/other.txt', bytes: 0 }
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => r.includes('missing-class')),
      `got: ${JSON.stringify(result.reasons)}`
    );
    assert.ok(
      result.reasons.some((r) => r.includes('minified-smoke') || /matrix/i.test(r)),
      `got: ${JSON.stringify(result.reasons)}`
    );
    assert.ok(
      result.reasons.some((r) => /mapping/i.test(r)),
      `got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('empty matrix fails closed', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, mapping } = greenArgs();
    const result = releaseAccept(warnings, [], mapping);
    assert.equal(result.ok, false);
  });

  it('missing mapping entry fails closed', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, matrix } = greenArgs();
    assert.equal(releaseAccept(warnings, matrix, undefined).ok, false);
    assert.equal(releaseAccept(warnings, matrix, null).ok, false);
  });
});

describe('T11 — fail-closed hardening (Stage 3 review MAJOR fix)', () => {
  it('blocker-kind with string count fails closed', () => {
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    const result = releaseAccept(
      [{ kind: 'missing-class', count: '2' }],
      matrix,
      mapping
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });

  it('blocker-kind with missing/NaN count fails closed', () => {
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    assert.equal(
      releaseAccept([{ kind: 'missing-class' },], matrix, mapping).ok,
      false
    );
    assert.equal(
      releaseAccept([{ kind: 'invalid-keep', count: NaN }], matrix, mapping).ok,
      false
    );
  });

  it('null warning entry fails closed', () => {
    const { releaseAccept } = loadImpl();
    const { matrix, mapping } = greenArgs();
    const result = releaseAccept([null], matrix, mapping);
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });

  it('non-string signal fails closed', () => {
    const { releaseAccept } = loadImpl();
    const { warnings, mapping } = greenArgs();
    const result = releaseAccept(
      warnings,
      [{ suite: 'minified-smoke', passed: true, signal: 42 }],
      mapping
    );
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });
});
