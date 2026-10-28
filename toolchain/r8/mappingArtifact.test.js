'use strict';
// T10 — mapping.txt present in release artifacts for symbolication
// (NFR-BUILD-007 / AC-BUILD-014).
// Hermetic: node:test + node:assert/strict (+ node:fs/node:path only to stat the
// tester-authored dist/mapping.txt placeholder fixture). Pure in-memory inputs
// otherwise; no network, no secrets, no live services.
// Red state: ./mappingArtifact.js does not exist yet, so every validator case
// below fails (MODULE_NOT_FOUND via loadImpl) until T10 lands.
//
// Interface under test (plan verbatim):
//   mappingPresent(entry: {path:string, bytes:number}): {ok:boolean, reason:string}
// ok:true only when path is dist/mapping.txt and bytes > 0.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function loadImpl() {
  return require('./mappingArtifact.js');
}

describe('T10 — contract shape (AC-BUILD-014)', () => {
  it('returns { ok: boolean, reason: string } for the correct non-empty entry', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/mapping.txt', bytes: 128 });
    assert.equal(typeof result.ok, 'boolean');
    assert.equal(typeof result.reason, 'string');
    assert.equal(result.ok, true);
  });
});

describe('T10 — correct non-empty entry passes (AC-BUILD-014)', () => {
  it('dist/mapping.txt with bytes > 0 passes', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/mapping.txt', bytes: 1 });
    assert.equal(result.ok, true);
  });

  it('dist/mapping.txt fixture exists and is non-empty', () => {
    const p = path.join(__dirname, '..', '..', 'dist', 'mapping.txt');
    const stat = fs.statSync(p);
    assert.ok(stat.size > 0, 'dist/mapping.txt must be non-empty');
  });
});

describe('T10 — wrong path, zero bytes, or missing entry fail (AC-BUILD-014)', () => {
  it('wrong path fails with a reason', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/other.txt', bytes: 128 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('zero bytes fails with a reason', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/mapping.txt', bytes: 0 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('negative bytes fails closed', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/mapping.txt', bytes: -5 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('missing entry (undefined) fails closed', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent(undefined);
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('missing path field fails', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ bytes: 128 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('missing bytes field fails', () => {
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/mapping.txt' });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('non-numeric bytes fails closed', () => {
    // ASSUMPTION: bytes must be a finite number > 0; strings fail even if numeric.
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: 'dist/mapping.txt', bytes: '128' });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('absolute path to mapping.txt fails (must be the literal artifact path)', () => {
    // ASSUMPTION: path must equal 'dist/mapping.txt' exactly, not merely end with it.
    const { mappingPresent } = loadImpl();
    const result = mappingPresent({ path: '/dist/mapping.txt', bytes: 128 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });
});
