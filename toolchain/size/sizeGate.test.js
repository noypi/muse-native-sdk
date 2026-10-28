'use strict';
// T7 — per-artifact size gate against perf footprint budgets
// (NFR-BUILD-010 / AC-BUILD-029, required dep AC-BUILD-072).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./sizeGate.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until T7 lands.
//
// Interface under test (plan verbatim):
//   checkSizes(artifacts: { name: string, bytes: number }[], budgets:
//   { name: string, bytes: number }[]): { ok: boolean, overruns: string[] }
// Budgets are passed in (imported from the perf reference by the caller,
// never redefined here); overruns name artifact and delta. Enforces the
// NFR-BUILD-019 rule: an overrun fails unless the budget itself changed in
// specs-performance.md — budgets are input, not owned, so the gate never
// edits a threshold.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./sizeGate.js');
}

describe('T7 — checkSizes contract shape (AC-BUILD-029)', () => {
  it('returns { ok: boolean, overruns: string[] } for an in-budget set', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'base.aab', bytes: 10 }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.overruns));
    assert.equal(result.ok, true);
    assert.deepEqual(result.overruns, []);
  });
});

describe('T7 — in-budget sets pass (AC-BUILD-029, AC-BUILD-072)', () => {
  it('artifacts exactly at budget pass (boundary: equal is not an overrun)', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'base.aab', bytes: 18 }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(result.ok, true);
    assert.deepEqual(result.overruns, []);
  });

  it('an empty artifact set passes with no overruns', () => {
    // ASSUMPTION: nothing shipped means nothing to measure — vacuously ok.
    const { checkSizes } = loadImpl();
    const result = checkSizes([], [{ name: 'base.aab', bytes: 18 }]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.overruns, []);
  });
});

describe('T7 — overruns fail naming artifact and delta (TC-BUILD-029)', () => {
  it('an inflated artifact trips the referenced budget with a named delta', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'base.aab', bytes: 20 }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(result.ok, false);
    assert.ok(result.overruns.length > 0, 'must report at least one overrun');
    const named = result.overruns.some(
      (o) => o.includes('base.aab') && o.includes('2')
    );
    assert.ok(
      named,
      `an overrun must name the artifact and its delta, got: ${JSON.stringify(result.overruns)}`
    );
  });

  it('one overrun among several artifacts fails and names only the offender', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [
        { name: 'base.aab', bytes: 10 },
        { name: 'feature-chat.apk', bytes: 30 },
      ],
      [
        { name: 'base.aab', bytes: 18 },
        { name: 'feature-chat.apk', bytes: 25 },
      ]
    );
    assert.equal(result.ok, false);
    const namesOffender = result.overruns.some((o) =>
      o.includes('feature-chat.apk')
    );
    assert.ok(
      namesOffender,
      `overruns must name the offending artifact, got: ${JSON.stringify(result.overruns)}`
    );
    const namesClean = result.overruns.some((o) => o.includes('base.aab'));
    assert.ok(
      !namesClean,
      `overruns must not name in-budget artifacts, got: ${JSON.stringify(result.overruns)}`
    );
  });

  it('an artifact with no matching budget is an overrun naming the artifact', () => {
    // ASSUMPTION: fail closed — an unbudgeted artifact cannot be shown to be
    // within the perf footprint envelope (AC-BUILD-072 records every
    // artifact against a referenced budget).
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'mystery-pack.zip', bytes: 5 }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(result.ok, false);
    const named = result.overruns.some((o) =>
      o.includes('mystery-pack.zip')
    );
    assert.ok(
      named,
      `an unbudgeted artifact must be reported by name, got: ${JSON.stringify(result.overruns)}`
    );
  });

  it('budgets are input, not owned: a caller-raised budget passes the same artifact', () => {    // NFR-BUILD-019 rule: the gate compares against the passed budgets; it
    // never redefines them. Same artifact passes when the caller passes the
    // updated perf budget.
    const { checkSizes } = loadImpl();
    const before = checkSizes(
      [{ name: 'base.aab', bytes: 20 }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(before.ok, false);
    const after = checkSizes(
      [{ name: 'base.aab', bytes: 20 }],
      [{ name: 'base.aab', bytes: 22 }]
    );
    assert.equal(after.ok, true);
    assert.deepEqual(after.overruns, []);
  });
});

describe('T7 — non-numeric/negative sizes fail closed (Stage 4 security pin, T7 LOW fix)', () => {
  it('artifact with undefined bytes fails closed naming the artifact', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'base.aab', bytes: undefined }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.overruns.some((o) => o.includes('base.aab')),
      `an invalid-size artifact must be reported by name, got: ${JSON.stringify(result.overruns)}`
    );
  });

  it('non-numeric budget bytes fail closed', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'base.aab', bytes: 10 }],
      [{ name: 'base.aab', bytes: 'x' }]
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.overruns.some((o) => o.includes('base.aab')),
      `an invalid budget must be reported by name, got: ${JSON.stringify(result.overruns)}`
    );
  });

  it('negative artifact bytes fail closed', () => {
    const { checkSizes } = loadImpl();
    const result = checkSizes(
      [{ name: 'base.aab', bytes: -5 }],
      [{ name: 'base.aab', bytes: 18 }]
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.overruns.some((o) => o.includes('base.aab')),
      `a negative-size artifact must be reported by name, got: ${JSON.stringify(result.overruns)}`
    );
  });
});
