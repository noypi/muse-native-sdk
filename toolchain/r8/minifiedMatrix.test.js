'use strict';
// T9 — Minified release must pass the full TEST-002 matrix; shrink breakages block
// (NFR-BUILD-007 / AC-BUILD-013; required dep TEST-002 — result rows consumed,
// matrix defined elsewhere; all rows below are synthetic test doubles).
// Hermetic: node:test + node:assert/strict only. Pure in-memory inputs; no
// filesystem, no network, no secrets, no live services.
// Red state: ./minifiedMatrix.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   matrixVerdict(results: {suite:string, passed:boolean, signal?:string}[]): {ok:boolean, blockers:string[]}
// ok:true only when every suite passed; any NoSuchMethodError/shrink signal
// or failed suite is a blocker naming the suite.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./minifiedMatrix.js');
}

function passing() {
  return [
    { suite: 'unit', passed: true },
    { suite: 'integration', passed: true },
    { suite: 'minified-smoke', passed: true },
  ];
}

describe('T9 — contract shape (AC-BUILD-013)', () => {
  it('returns { ok: boolean, blockers: string[] } for an all-pass matrix', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict(passing());
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.blockers));
    assert.equal(result.ok, true);
    assert.deepEqual(result.blockers, []);
  });
});

describe('T9 — all-pass matrix passes (AC-BUILD-013, TEST-002)', () => {
  it('every suite passed with no signals is ok', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict(passing());
    assert.equal(result.ok, true);
    assert.deepEqual(result.blockers, []);
  });

  it('single-suite matrix passing is ok', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([{ suite: 'minified-smoke', passed: true }]);
    assert.equal(result.ok, true);
  });
});

describe('T9 — failures and shrink signals block naming the suite', () => {
  it('single failed suite fails naming that suite', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([
      { suite: 'unit', passed: true },
      { suite: 'minified-smoke', passed: false },
    ]);
    assert.equal(result.ok, false);
    assert.ok(
      result.blockers.some((b) => b.includes('minified-smoke')),
      `blockers must name the suite, got: ${JSON.stringify(result.blockers)}`
    );
  });

  it('NoSuchMethodError signal on a passed suite is still a blocker', () => {
    // Shrink breakage evidence blocks even if the row claims passed.
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([
      { suite: 'unit', passed: true },
      {
        suite: 'minified-smoke',
        passed: true,
        signal: 'NoSuchMethodError: com.example.Foo.bar',
      },
    ]);
    assert.equal(result.ok, false);
    assert.ok(
      result.blockers.some((b) => b.includes('minified-smoke')),
      `blockers must name the suite, got: ${JSON.stringify(result.blockers)}`
    );
  });

  it('failed suite with NoSuchMethodError signal names the suite once at least', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([
      {
        suite: 'integration',
        passed: false,
        signal: 'NoSuchMethodError: com.example.Baz.qux',
      },
    ]);
    assert.equal(result.ok, false);
    assert.ok(result.blockers.some((b) => b.includes('integration')));
  });

  it('shrink-style signal (missing class/method) blocks', () => {
    // ASSUMPTION: any signal mentioning NoSuchMethodError, ClassNotFound, or
    // shrink counts as a shrink-breakage blocker, not only the literal
    // NoSuchMethodError token.
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([
      {
        suite: 'minified-smoke',
        passed: true,
        signal: 'ClassNotFoundException after shrink: com.example.Foo',
      },
    ]);
    assert.equal(result.ok, false);
    assert.ok(result.blockers.some((b) => b.includes('minified-smoke')));
  });

  it('multiple failures name every failing suite', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([
      { suite: 'unit', passed: false },
      { suite: 'integration', passed: true },
      { suite: 'minified-smoke', passed: false },
    ]);
    assert.equal(result.ok, false);
    assert.ok(result.blockers.some((b) => b.includes('unit')));
    assert.ok(result.blockers.some((b) => b.includes('minified-smoke')));
    assert.ok(!result.blockers.some((b) => b.includes('integration')));
  });

  it('benign signal text on a passed suite does not block', () => {
    // ASSUMPTION: empty/absent signal is benign; only shrink-class signals
    // block. A passed row with an empty signal string stays green.
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([{ suite: 'unit', passed: true, signal: '' }]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.blockers, []);
  });
});

describe('T9 — malformed input fails closed', () => {
  it('empty matrix fails closed', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([]);
    assert.equal(result.ok, false);
    assert.ok(result.blockers.length > 0);
  });

  it('non-array input fails closed', () => {
    const { matrixVerdict } = loadImpl();
    assert.equal(matrixVerdict(undefined).ok, false);
    assert.equal(matrixVerdict(null).ok, false);
    assert.equal(matrixVerdict('green').ok, false);
  });

  it('row with missing suite name fails closed', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([{ passed: true }]);
    assert.equal(result.ok, false);
  });

  it('row with non-boolean passed fails closed', () => {
    const { matrixVerdict } = loadImpl();
    const result = matrixVerdict([{ suite: 'unit', passed: 'yes' }]);
    assert.equal(result.ok, false);
  });
});
