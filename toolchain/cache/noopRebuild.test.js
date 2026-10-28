'use strict';
// T3b — No-op rebuild gate (NFR-BUILD-011 / AC-BUILD-035; repoints
// TC-BUILD-035 first half: empty change set + identical hashes +
// near-total hit passes).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./incrementalCycle.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim, verifyNoop half):
//   verifyNoop({changedFiles:string[], artifactHashBefore:string, artifactHashAfter:string, cacheHitRate:number}): {ok:boolean, reasons:string[]} — verifyNoop fails with named reasons when changedFiles non-empty, hashes differ, or hit rate below 0.99.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./incrementalCycle.js');
}

function greenArgs() {
  return {
    changedFiles: [],
    artifactHashBefore: 'artifact-hash-fixture-001',
    artifactHashAfter: 'artifact-hash-fixture-001',
    cacheHitRate: 1,
  };
}

describe('T3b — contract shape (AC-BUILD-035)', () => {
  it('returns { ok: boolean, reasons: string[] } with empty reasons on the green path', () => {
    const { verifyNoop } = loadImpl();
    const result = verifyNoop(greenArgs());
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.reasons));
    assert.equal(result.ok, true);
    assert.deepEqual(result.reasons, []);
  });
});

describe('T3b — no-op green path (AC-BUILD-035)', () => {
  it('empty change set + identical hashes + hit rate 1 passes', () => {
    const { verifyNoop } = loadImpl();
    assert.equal(verifyNoop(greenArgs()).ok, true);
  });

  it('hit rate exactly 0.99 passes (boundary: below 0.99 fails)', () => {
    // ASSUMPTION: the 0.99 floor is inclusive — exactly 0.99 is a pass.
    const { verifyNoop } = loadImpl();
    const args = greenArgs();
    args.cacheHitRate = 0.99;
    assert.equal(verifyNoop(args).ok, true);
  });
});

describe('T3b — each failing leg fails with named reasons (AC-BUILD-035)', () => {
  it('non-empty changedFiles fails naming the change leg', () => {
    const { verifyNoop } = loadImpl();
    const args = greenArgs();
    args.changedFiles = ['lib.cc'];
    const result = verifyNoop(args);
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => r.includes('lib.cc') || /chang/i.test(r)),
      `reasons must name the change leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('differing artifact hashes fail naming the hash leg', () => {
    const { verifyNoop } = loadImpl();
    const args = greenArgs();
    args.artifactHashAfter = 'artifact-hash-fixture-002';
    const result = verifyNoop(args);
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => /hash|artifact/i.test(r)),
      `reasons must name the hash leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('hit rate below 0.99 fails naming the rate leg', () => {
    const { verifyNoop } = loadImpl();
    const args = greenArgs();
    args.cacheHitRate = 0.98;
    const result = verifyNoop(args);
    assert.equal(result.ok, false);
    assert.ok(
      result.reasons.some((r) => /hit|rate|0\.99/.test(r)),
      `reasons must name the rate leg, got: ${JSON.stringify(result.reasons)}`
    );
  });

  it('combined failures name every failing leg', () => {
    const { verifyNoop } = loadImpl();
    const result = verifyNoop({
      changedFiles: ['lib.cc'],
      artifactHashBefore: 'artifact-hash-fixture-001',
      artifactHashAfter: 'artifact-hash-fixture-002',
      cacheHitRate: 0.5,
    });
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length >= 3, `expected 3 reasons, got: ${JSON.stringify(result.reasons)}`);
  });
});

describe('T3b — fail-closed hardening', () => {
  it('missing hashes fail closed', () => {
    const { verifyNoop } = loadImpl();
    const args = greenArgs();
    delete args.artifactHashAfter;
    const result = verifyNoop(args);
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });

  it('missing input fails closed', () => {
    const { verifyNoop } = loadImpl();
    assert.equal(verifyNoop(undefined).ok, false);
    assert.equal(verifyNoop(null).ok, false);
  });

  it('non-numeric hit rate fails closed', () => {
    const { verifyNoop } = loadImpl();
    const args = greenArgs();
    args.cacheHitRate = 'high';
    const result = verifyNoop(args);
    assert.equal(result.ok, false);
    assert.ok(result.reasons.length > 0);
  });
});
