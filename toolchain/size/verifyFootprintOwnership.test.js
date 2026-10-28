'use strict';
// T1 — footprint-ownership gate: caller budgets must equal the perf reference
// (NFR-BUILD-004 / AC-BUILD-004; required dep AC-PERFORMANCE-009, reference
// values only, synthetic in tests).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./verifyFootprintOwnership.js does not exist yet, so every case
// below fails (MODULE_NOT_FOUND via loadImpl) until T1 lands.
//
// Interface under test (plan verbatim):
//   verifyOwnership(callerBudgets: {name:string, bytes:number}[], perfReference:
//   {name:string, bytes:number}[]): {ok:boolean, violations:string[]}
// Budgets are input imported from the perf reference by the caller, never
// redefined here; the gate never writes thresholds.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./verifyFootprintOwnership.js');
}

describe('T1 — contract shape (AC-BUILD-004)', () => {
  it('returns { ok: boolean, violations: string[] } for identical inputs', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: 15 }],
      [{ name: 'base.apk', bytes: 15 }]
    );
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.violations));
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T1 — identical caller/reference passes (AC-BUILD-004)', () => {
  it('matching values for every artifact pass', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [
        { name: 'base.apk', bytes: 15 },
        { name: 'dynamic-chat', bytes: 20 },
      ],
      [
        { name: 'base.apk', bytes: 15 },
        { name: 'dynamic-chat', bytes: 20 },
      ]
    );
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });

  it('empty caller budgets pass vacuously', () => {
    // ASSUMPTION: nothing claimed means nothing to contradict the reference.
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership([], [{ name: 'base.apk', bytes: 15 }]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.violations, []);
  });
});

describe('T1 — locally edited budgets fail (AC-BUILD-004)', () => {
  it('a locally-raised threshold fails naming the artifact', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: 18 }],
      [{ name: 'base.apk', bytes: 15 }]
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('base.apk')),
      `violation must name the artifact, got: ${JSON.stringify(result.violations)}`
    );
  });

  it('a locally-lowered threshold also fails (any difference is a violation)', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: 12 }],
      [{ name: 'base.apk', bytes: 15 }]
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('base.apk')));
  });

  it('one edited entry among several fails and names only the offender', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [
        { name: 'base.apk', bytes: 15 },
        { name: 'dynamic-chat', bytes: 25 },
      ],
      [
        { name: 'base.apk', bytes: 15 },
        { name: 'dynamic-chat', bytes: 20 },
      ]
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('dynamic-chat')));
    assert.ok(!result.violations.some((v) => v.includes('base.apk')));
  });
});

describe('T1 — artifact lacking a reference entry fails (AC-BUILD-004)', () => {
  it('an unbudgeted caller artifact fails naming the artifact', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'mystery-pack.zip', bytes: 5 }],
      [{ name: 'base.apk', bytes: 15 }]
    );
    assert.equal(result.ok, false);
    assert.ok(
      result.violations.some((v) => v.includes('mystery-pack.zip')),
      `violation must name the artifact, got: ${JSON.stringify(result.violations)}`
    );
  });
});

describe('T1 — non-numeric/negative input fails closed', () => {
  it('non-numeric caller bytes fail naming the artifact', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: 'x' }],
      [{ name: 'base.apk', bytes: 15 }]
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('base.apk')));
  });

  it('negative caller bytes fail closed', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: -3 }],
      [{ name: 'base.apk', bytes: 15 }]
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('base.apk')));
  });

  it('non-numeric reference bytes fail closed', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: 15 }],
      [{ name: 'base.apk', bytes: undefined }]
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('base.apk')));
  });

  it('negative reference bytes fail closed', () => {
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(
      [{ name: 'base.apk', bytes: 15 }],
      [{ name: 'base.apk', bytes: -1 }]
    );
    assert.equal(result.ok, false);
    assert.ok(result.violations.some((v) => v.includes('base.apk')));
  });

  it('non-array inputs fail closed', () => {
    // ASSUMPTION: malformed inputs cannot prove ownership, so they fail.
    const { verifyOwnership } = loadImpl();
    const result = verifyOwnership(null, null);
    assert.equal(result.ok, false);
    assert.ok(result.violations.length > 0);
  });
});
