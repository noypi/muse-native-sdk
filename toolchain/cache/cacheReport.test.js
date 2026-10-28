'use strict';
// T4 — Cache reporting (NFR-BUILD-014?/AC-BUILD-034; TC-BUILD-034).
// Hermetic: node:test + node:assert/strict only. Pure in-memory calls; no
// filesystem, no network, no secrets, no live services.
// Red state: ./cacheReport.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   reportCache({hits:number, misses:number, floor:number}): {hitRate:number, ok:boolean, warning:string|null}

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./cacheReport.js');
}

describe('T4 — happy path (AC-BUILD-034)', () => {
  it('computes hitRate as hits/(hits+misses) and reports ok with null warning', () => {
    const { reportCache } = loadImpl();
    const result = reportCache({ hits: 3, misses: 1, floor: 0.5 });
    assert.equal(result.hitRate, 0.75);
    assert.equal(result.ok, true);
    assert.equal(result.warning, null);
  });

  it('returns hitRate 0 when there are no hits', () => {
    const { reportCache } = loadImpl();
    const result = reportCache({ hits: 0, misses: 4, floor: 0.5 });
    assert.equal(result.hitRate, 0);
    assert.equal(result.ok, false);
  });

  it('returns hitRate 1 when there are no misses', () => {
    const { reportCache } = loadImpl();
    const result = reportCache({ hits: 5, misses: 0, floor: 0.5 });
    assert.equal(result.hitRate, 1);
    assert.equal(result.ok, true);
    assert.equal(result.warning, null);
  });
});

describe('T4 — floor boundary (AC-BUILD-034)', () => {
  it('hitRate equal to floor is ok with null warning', () => {
    const { reportCache } = loadImpl();
    const result = reportCache({ hits: 1, misses: 1, floor: 0.5 });
    assert.equal(result.hitRate, 0.5);
    assert.equal(result.ok, true);
    assert.equal(result.warning, null);
  });

  it('hitRate below floor is not ok and warning names the floor', () => {
    const { reportCache } = loadImpl();
    const result = reportCache({ hits: 1, misses: 3, floor: 0.5 });
    assert.equal(result.hitRate, 0.25);
    assert.equal(result.ok, false);
    assert.equal(typeof result.warning, 'string');
    assert.ok(
      result.warning.includes('0.5'),
      `warning must name the floor, got: ${JSON.stringify(result.warning)}`
    );
  });

  it('zero-total cache reports hitRate 1 (vacuously cached no-op)', () => {
    // ASSUMPTION: plan is silent on hits=0,misses=0; 1 mirrors the
    // incremental-cycle empty-set convention (fully cached no-op).
    const { reportCache } = loadImpl();
    const result = reportCache({ hits: 0, misses: 0, floor: 0.5 });
    assert.equal(result.hitRate, 1);
    assert.equal(result.ok, true);
    assert.equal(result.warning, null);
  });
});

describe('T4 — error cases', () => {
  it('throws on negative hits', () => {
    const { reportCache } = loadImpl();
    assert.throws(
      () => reportCache({ hits: -1, misses: 2, floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
  });

  it('throws on negative misses', () => {
    const { reportCache } = loadImpl();
    assert.throws(
      () => reportCache({ hits: 2, misses: -1, floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
  });

  it('throws on non-numeric counts', () => {
    const { reportCache } = loadImpl();
    assert.throws(
      () => reportCache({ hits: '3', misses: 1, floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
    assert.throws(
      () => reportCache({ hits: 3, misses: '1', floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
    assert.throws(
      () => reportCache({ hits: NaN, misses: 1, floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
    assert.throws(
      () => reportCache({ hits: undefined, misses: 1, floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
    assert.throws(
      () => reportCache({ hits: 1, misses: null, floor: 0.5 }),
      /cache-report: non-negative counts required/
    );
  });
});
