'use strict';
// T5 — Per-artifact size report vs referenced perf budgets (NFR-BUILD-019 / AC-BUILD-072, TC-BUILD-072).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./sizeReport.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until T5 lands.
//
// Interface under test (plan verbatim):
//   recordSizes(artifacts: Array<{ name: string, downloadBytes: number, installBytes: number }>, budgets: Record<string, number>): { rows: Array<{ name, downloadBytes, installBytes, budget, over: boolean }>, failures: string[] } — unknown artifact names throw SizeError('unknown-artifact'); budgets object is imported from perf (never defined here).
//
// ASSUMPTIONS:
//   A1: `budgets` maps artifact name -> budget bytes (a single number per
//       artifact). A row is `over: true` when EITHER downloadBytes OR
//       installBytes exceeds the named budget; equal-to-budget is not over.
//   A2: Every entry in `failures` names the offending artifact (and, when
//       over budget, the delta) so the release report can re-tune the Play
//       delivery strategy from data (AC-BUILD-072/074 context).
//   A3: Unknown artifact = a name with no key in `budgets`; fail-closed by
//       throwing SizeError('unknown-artifact') rather than recording a row.
//       Thrown errors expose err.code; the helper also accepts the code in
//       err.message.
//   A4: Non-finite or negative sizes fail closed: the row is marked over and
//       named in failures (never silently passes).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./sizeReport.js');
}

// Budgets are IMPORTED from perf by the caller (never defined here): this
// fixture mirrors perf binary_and_footprint_estimates by reference only.
const PERF_BUDGETS = {
  'base.aab': 18_000_000,
  'split-arm64.apk': 8_000_000,
  'assets-pack-main.zip': 200_000_000,
  'feature-chat.zip': 25_000_000,
};

function expectSizeError(fn, code) {
  let thrown = null;
  try {
    fn();
  } catch (err) {
    thrown = err;
  }
  assert.ok(thrown, `expected SizeError('${code}') to be thrown`);
  const blob = `${(thrown && thrown.code) || ''} ${(thrown && thrown.message) || ''}`;
  assert.ok(
    (thrown && thrown.code) === code || blob.includes(code),
    `expected error code '${code}', got code=${JSON.stringify(thrown && thrown.code)} message=${JSON.stringify(thrown && thrown.message)}`
  );
  assert.ok(
    (thrown && thrown.name) === 'SizeError' || blob.includes('SizeError') || blob.includes(code),
    `expected a SizeError, got name=${JSON.stringify(thrown && thrown.name)}`
  );
}

describe('T5 — recordSizes contract shape (AC-BUILD-072)', () => {
  it('returns { rows, failures } for an in-budget set', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 10_000_000, installBytes: 12_000_000 }],
      PERF_BUDGETS
    );
    assert.ok(Array.isArray(result.rows), 'rows must be an array');
    assert.ok(Array.isArray(result.failures), 'failures must be an array');
    assert.equal(result.rows.length, 1);
    assert.deepEqual(result.failures, []);
  });

  it('each row echoes name, downloadBytes, installBytes, budget, over', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 10_000_000, installBytes: 12_000_000 }],
      PERF_BUDGETS
    );
    const row = result.rows[0];
    assert.equal(row.name, 'base.aab');
    assert.equal(row.downloadBytes, 10_000_000);
    assert.equal(row.installBytes, 12_000_000);
    assert.equal(row.budget, PERF_BUDGETS['base.aab']);
    assert.equal(row.over, false);
  });

  it('covers the BUILD-006 taxonomy: base AAB, split, asset pack, feature module', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [
        { name: 'base.aab', downloadBytes: 1_000, installBytes: 2_000 },
        { name: 'split-arm64.apk', downloadBytes: 1_000, installBytes: 2_000 },
        { name: 'assets-pack-main.zip', downloadBytes: 1_000, installBytes: 2_000 },
        { name: 'feature-chat.zip', downloadBytes: 1_000, installBytes: 2_000 },
      ],
      PERF_BUDGETS
    );
    assert.equal(result.rows.length, 4);
    assert.deepEqual(result.failures, []);
  });

  it('an empty artifact set yields empty rows and no failures', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes([], PERF_BUDGETS);
    assert.deepEqual(result.rows, []);
    assert.deepEqual(result.failures, []);
  });
});

describe('T5 — budget comparison against referenced perf budgets (AC-BUILD-072)', () => {
  it('sizes exactly at budget pass (boundary: equal is not over)', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: PERF_BUDGETS['base.aab'], installBytes: PERF_BUDGETS['base.aab'] }],
      PERF_BUDGETS
    );
    assert.equal(result.rows[0].over, false);
    assert.deepEqual(result.failures, []);
  });

  it('an installBytes overrun marks the row over and names it in failures', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 1_000, installBytes: PERF_BUDGETS['base.aab'] + 2 }],
      PERF_BUDGETS
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.length > 0, 'must report at least one failure');
    assert.ok(
      result.failures.some((f) => f.includes('base.aab')),
      `failures must name the artifact, got: ${JSON.stringify(result.failures)}`
    );
  });

  it('a downloadBytes overrun marks the row over even when installBytes fits', () => {
    // ASSUMPTION A1: over if EITHER measure exceeds the referenced budget.
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'feature-chat.zip', downloadBytes: PERF_BUDGETS['feature-chat.zip'] + 1, installBytes: 100 }],
      PERF_BUDGETS
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('feature-chat.zip')));
  });

  it('budgets are input, not owned: a caller-raised budget passes the same artifact', () => {
    // NFR-BUILD-019 rule: the report compares against the passed budgets
    // object (imported from perf); it never redefines thresholds locally.
    const { recordSizes } = loadImpl();
    const artifact = { name: 'base.aab', downloadBytes: 1_000, installBytes: PERF_BUDGETS['base.aab'] + 5 };
    const before = recordSizes([artifact], PERF_BUDGETS);
    assert.equal(before.rows[0].over, true);
    const raised = { ...PERF_BUDGETS, 'base.aab': PERF_BUDGETS['base.aab'] + 100 };
    const after = recordSizes([artifact], raised);
    assert.equal(after.rows[0].over, false);
    assert.deepEqual(after.failures, []);
  });
});

describe("T5 — unknown artifacts throw SizeError('unknown-artifact')", () => {
  it('throws for a name with no perf budget entry', () => {
    const { recordSizes } = loadImpl();
    expectSizeError(
      () => recordSizes(
        [{ name: 'mystery-pack.zip', downloadBytes: 5, installBytes: 5 }],
        PERF_BUDGETS
      ),
      'unknown-artifact'
    );
  });

  it('one unknown name among known artifacts still throws', () => {
    const { recordSizes } = loadImpl();
    expectSizeError(
      () => recordSizes(
        [
          { name: 'base.aab', downloadBytes: 1_000, installBytes: 2_000 },
          { name: 'nope.zip', downloadBytes: 1, installBytes: 1 },
        ],
        PERF_BUDGETS
      ),
      'unknown-artifact'
    );
  });
});

describe('T5 — invalid sizes fail closed (security pin)', () => {
  it('negative installBytes is over and named in failures', () => {
    // ASSUMPTION A4: fail closed, never silently pass.
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 1_000, installBytes: -5 }],
      PERF_BUDGETS
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('base.aab')));
  });

  it('NaN downloadBytes is over and named in failures', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: NaN, installBytes: 1_000 }],
      PERF_BUDGETS
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('base.aab')));
  });

  it('undefined sizes are over and named in failures', () => {
    const { recordSizes } = loadImpl();
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: undefined, installBytes: undefined }],
      PERF_BUDGETS
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('base.aab')));
  });

  it('string budget fails closed: row over and named in failures', () => {
    // SECURITY Finding 2: non-numeric budget must not silently pass.
    const { recordSizes } = loadImpl();
    const budgets = { ...PERF_BUDGETS, 'base.aab': 'huge' };
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 1000, installBytes: 1000 }],
      budgets
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('base.aab')));
  });

  it('NaN budget fails closed: row over and named in failures', () => {
    const { recordSizes } = loadImpl();
    const budgets = { ...PERF_BUDGETS, 'base.aab': NaN };
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 1000, installBytes: 1000 }],
      budgets
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('base.aab')));
  });

  it('negative budget fails closed: row over and named in failures', () => {
    const { recordSizes } = loadImpl();
    const budgets = { ...PERF_BUDGETS, 'base.aab': -5 };
    const result = recordSizes(
      [{ name: 'base.aab', downloadBytes: 1000, installBytes: 1000 }],
      budgets
    );
    assert.equal(result.rows[0].over, true);
    assert.ok(result.failures.some((f) => f.includes('base.aab')));
  });
});
