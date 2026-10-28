'use strict';
// T5 — Macrobenchmark delta comparator proves startup improvement before accept
// (NFR-BUILD-006 / AC-BUILD-009).
// Hermetic: node:test + node:assert/strict only. Pure in-memory numeric
// fixtures (no live device); no filesystem, no network, no secrets.
// Red state: ./macrobenchmark.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   compareStartup(baselineMs: number[], candidateMs: number[]): {ok:boolean, deltaMs:number, reason:string}
// ok:true only when candidate median is strictly lower than baseline median
// on valid non-empty numeric inputs; invalid/empty inputs fail closed.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./macrobenchmark.js');
}

describe('T5 — contract shape (AC-BUILD-009)', () => {
  it('returns { ok: boolean, deltaMs: number, reason: string } for improving input', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([200, 210, 220], [150, 160, 170]);
    assert.equal(typeof result.ok, 'boolean');
    assert.equal(typeof result.deltaMs, 'number');
    assert.equal(typeof result.reason, 'string');
    assert.equal(result.ok, true);
  });
});

describe('T5 — improvement verdict (AC-BUILD-009)', () => {
  it('strictly lower candidate median passes with a negative delta', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([200, 210, 220], [150, 160, 170]);
    assert.equal(result.ok, true);
    assert.ok(result.deltaMs < 0, `delta must be negative, got ${result.deltaMs}`);
    assert.equal(result.deltaMs, 160 - 210);
  });

  it('regressed candidate fails with a positive delta', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([150, 160, 170], [200, 210, 220]);
    assert.equal(result.ok, false);
    assert.ok(result.deltaMs > 0);
    assert.ok(result.reason.length > 0);
  });

  it('equal medians fail (strictly lower required)', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([200, 200, 200], [200, 200, 200]);
    assert.equal(result.ok, false);
    assert.equal(result.deltaMs, 0);
  });

  it('median uses the middle value, not the mean (outlier resistance)', () => {
    // ASSUMPTION: median is the middle of the sorted values; for even-length
    // arrays the median is the mean of the two middle values.
    // Mean would say the candidate regressed (700/3 > 600/3); median says
    // it improved (150 < 200).
    const { compareStartup } = loadImpl();
    const result = compareStartup([200, 200, 200], [100, 150, 1800]);
    assert.equal(result.ok, true);
    assert.equal(result.deltaMs, 150 - 200);
  });

  it('even-length arrays compare the averaged middle pair', () => {
    // ASSUMPTION: median([100, 200]) === 150.
    const { compareStartup } = loadImpl();
    const result = compareStartup([100, 200], [50, 100]);
    assert.equal(result.ok, true);
    assert.equal(result.deltaMs, 75 - 150);
  });

  it('single-sample arrays compare directly', () => {
    const { compareStartup } = loadImpl();
    assert.equal(compareStartup([300], [250]).ok, true);
    assert.equal(compareStartup([250], [300]).ok, false);
  });

  it('unsorted inputs are sorted before the median, not taken positionally', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([220, 200, 210], [170, 150, 160]);
    assert.equal(result.ok, true);
    assert.equal(result.deltaMs, 160 - 210);
  });
});

describe('T5 — invalid inputs fail closed', () => {
  it('empty baseline fails closed', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([], [150, 160]);
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('empty candidate fails closed', () => {
    const { compareStartup } = loadImpl();
    const result = compareStartup([200, 210], []);
    assert.equal(result.ok, false);
  });

  it('non-numeric entries fail closed', () => {
    const { compareStartup } = loadImpl();
    assert.equal(compareStartup([200, 'fast', 210], [150, 160]).ok, false);
    assert.equal(compareStartup([200, 210], [150, NaN]).ok, false);
  });

  it('non-positive timings fail closed', () => {
    // ASSUMPTION: startup timings must be finite numbers > 0; zero/negative/
    // infinite values are fixture errors, not measurements.
    const { compareStartup } = loadImpl();
    assert.equal(compareStartup([0, 200], [150, 160]).ok, false);
    assert.equal(compareStartup([200, 210], [-5, 160]).ok, false);
    assert.equal(compareStartup([200, Infinity], [150, 160]).ok, false);
  });

  it('non-array inputs fail closed', () => {
    const { compareStartup } = loadImpl();
    assert.equal(compareStartup(undefined, [150]).ok, false);
    assert.equal(compareStartup([200], null).ok, false);
    assert.equal(compareStartup('200', [150]).ok, false);
  });
});
