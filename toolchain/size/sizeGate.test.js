'use strict';
// MERGED — T8 RE-PUBLISH (GR-08 T1.10 augment-don't-replace).
// Suite A (T7 / NFR-BUILD-010 / AC-BUILD-029, TC-BUILD-029): original
// `checkSizes(artifacts, budgets)` cases, restored verbatim in intent from
// `git show HEAD:toolchain/size/sizeGate.test.js`. Production
// `checkSizes` stays intact; these cases must stay GREEN.
// Suite B (T8 / NFR-BUILD-019 / AC-BUILD-075, TC-BUILD-075): current T8
// `checkSizeGate(report)` cases per plan Interface line. Behavior missing,
// so these cases are RED until the coder adds `checkSizeGate`.
//
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./sizeGate.js');
}

function gateOf(mod) {
  if (typeof mod.checkSizeGate === 'function') return mod.checkSizeGate;
  throw new Error(
    "checkSizeGate missing: expected `checkSizeGate(report)` per T8 contract (got exports: " +
      Object.keys(mod).join(', ') +
      ')'
  );
}

// ── Suite A — T7 checkSizes (TC-BUILD-029 preservation) ──
// Interface (plan verbatim):
//   checkSizes(artifacts: { name: string, bytes: number }[], budgets:
//   { name: string, bytes: number }[]): { ok: boolean, overruns: string[] }

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

  it('budgets are input, not owned: a caller-raised budget passes the same artifact', () => {
    // NFR-BUILD-019 rule: the gate compares against the passed budgets; it
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

// ── Suite B — T8 checkSizeGate (TC-BUILD-075) ──
// Interface (plan verbatim):
//   checkSizeGate(report: { rows: Array<{ name, installBytes, budget }> }): { pass: boolean, deltas: Array<{ name, delta }> }

describe('T8 — gate shape (AC-BUILD-075)', () => {
  it('returns { pass: boolean, deltas: Array<{ name, delta }> } for an in-budget report', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [{ name: 'base.aab', installBytes: 10, budget: 18 }],
    });
    assert.equal(typeof result.pass, 'boolean');
    assert.ok(Array.isArray(result.deltas));
    assert.equal(result.pass, true);
    assert.deepEqual(result.deltas, []);
  });

  it('empty rows pass with no deltas', () => {
    // ASSUMPTION: nothing shipped means nothing to measure — vacuously pass.
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({ rows: [] });
    assert.equal(result.pass, true);
    assert.deepEqual(result.deltas, []);
  });

  it('a row exactly at budget passes (boundary: equal is not an overrun)', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [{ name: 'base.aab', installBytes: 18, budget: 18 }],
    });
    assert.equal(result.pass, true);
    assert.deepEqual(result.deltas, []);
  });
});

describe('T8 — inflated asset trips the referenced budget (TC-BUILD-075)', () => {
  it('an inflated asset shows up as a per-artifact delta tripping the budget', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [{ name: 'asset-pack-hero', installBytes: 30, budget: 25 }],
    });
    assert.equal(result.pass, false);
    assert.ok(result.deltas.length > 0, 'must report at least one delta');
    const hit = result.deltas.find((d) => d.name === 'asset-pack-hero');
    assert.ok(hit, `deltas must name the inflated artifact, got: ${JSON.stringify(result.deltas)}`);
    assert.equal(hit.delta, 5);
  });

  it('never silently passes an over-budget row', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [
        { name: 'base.aab', installBytes: 10, budget: 18 },
        { name: 'asset-pack-hero', installBytes: 26, budget: 25 },
      ],
    });
    assert.equal(result.pass, false);
    const namesClean = result.deltas.some((d) => d.name === 'base.aab');
    assert.ok(!namesClean, `in-budget rows must not appear in deltas, got: ${JSON.stringify(result.deltas)}`);
    assert.ok(
      result.deltas.some((d) => d.name === 'asset-pack-hero'),
      `over-budget row must appear in deltas, got: ${JSON.stringify(result.deltas)}`
    );
  });
});

describe('T8 — synthetic native-dep entry trips the referenced budget (TC-BUILD-075)', () => {
  it('an added synthetic native-dep entry shows up as a per-artifact delta', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [
        { name: 'base.aab', installBytes: 10, budget: 18 },
        { name: 'libnative-dep.so', installBytes: 12, budget: 8 },
      ],
    });
    assert.equal(result.pass, false);
    const hit = result.deltas.find((d) => d.name === 'libnative-dep.so');
    assert.ok(hit, `deltas must name the synthetic native dep, got: ${JSON.stringify(result.deltas)}`);
    assert.equal(hit.delta, 4);
  });

  it('inflated asset AND native dep both appear as deltas together', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [
        { name: 'asset-pack-hero', installBytes: 30, budget: 25 },
        { name: 'libnative-dep.so', installBytes: 12, budget: 8 },
      ],
    });
    assert.equal(result.pass, false);
    assert.equal(result.deltas.length, 2);
    const names = result.deltas.map((d) => d.name).sort();
    assert.deepEqual(names, ['asset-pack-hero', 'libnative-dep.so']);
    for (const d of result.deltas) {
      assert.equal(typeof d.delta, 'number');
      assert.ok(d.delta > 0, `delta must be positive, got: ${JSON.stringify(d)}`);
    }
  });
});

describe('T8 — fail-closed inputs (security pin)', () => {
  it('non-numeric installBytes fails closed naming the artifact', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [{ name: 'asset-pack-hero', installBytes: undefined, budget: 25 }],
    });
    assert.equal(result.pass, false);
    assert.ok(
      result.deltas.some((d) => d.name === 'asset-pack-hero'),
      `invalid-size row must be reported by name, got: ${JSON.stringify(result.deltas)}`
    );
  });

  it('negative installBytes fails closed', () => {
    const checkSizeGate = gateOf(loadImpl());
    const result = checkSizeGate({
      rows: [{ name: 'asset-pack-hero', installBytes: -3, budget: 25 }],
    });
    assert.equal(result.pass, false);
    assert.ok(result.deltas.some((d) => d.name === 'asset-pack-hero'));
  });
});
