'use strict';
// T4 — FR-DX-002 (AC-DX-007): dev-loop reload-time budget harness.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no secrets,
// no live services, no clock, no env. The perf budget is consumed from the
// perf-owned manifest at `benchmark/budgets.json` (spec TC-PERFORMANCE-003 path);
// this file never defines a numeric budget of its own.
// ASSUMPTIONS (plan genuinely silent on the manifest path/shape; most defensible
// behavior tested — marked ASSUMPTION in T4.md):
// - A-T4a: perf owns `benchmark/budgets.json` exposing numeric `reloadBudgetMs`.
// - A-T4b: `measureReload` itself emits the `{elapsedMs, budgetMs, withinBudget}`
//   JSON line to stdout on every call (Interface + Detail combined reading).
// - A-T4c: inverted marks (visibleTs < saveTs) throw RangeError; non-numeric
//   marks throw TypeError.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { measureReload } = require('../run.js');
const budgets = require('../../budgets.json');

const BUDGET_MS = budgets.reloadBudgetMs;
assert.ok(
  Number.isFinite(BUDGET_MS),
  'perf manifest must expose numeric reloadBudgetMs',
);

function captureStdout(fn) {
  const orig = process.stdout.write;
  let out = '';
  process.stdout.write = (chunk) => {
    out += String(chunk);
    return true;
  };
  try {
    fn();
  } finally {
    process.stdout.write = orig;
  }
  return out;
}

describe('measureReload — save-to-visible interval', () => {
  it('returns elapsedMs as visibleTs minus saveTs', () => {
    const result = measureReload({ saveTs: 200, visibleTs: 1200 });
    assert.equal(result.elapsedMs, 1000);
  });

  it('returns exactly the { elapsedMs, withinBudget } shape', () => {
    const result = measureReload({ saveTs: 0, visibleTs: 10 });
    assert.deepEqual(Object.keys(result).sort(), ['elapsedMs', 'withinBudget']);
  });

  it('reports withinBudget true when elapsed is under the perf budget', () => {
    const result = measureReload({ saveTs: 0, visibleTs: BUDGET_MS - 1 });
    assert.equal(result.withinBudget, true);
  });

  it('reports withinBudget true at the exact budget boundary', () => {
    const result = measureReload({ saveTs: 0, visibleTs: BUDGET_MS });
    assert.equal(result.withinBudget, true);
  });

  it('reports withinBudget false when elapsed exceeds the perf budget', () => {
    const result = measureReload({ saveTs: 0, visibleTs: BUDGET_MS + 1 });
    assert.equal(result.withinBudget, false);
  });

  it('publishes one {elapsedMs, budgetMs, withinBudget} JSON line to stdout', () => {
    const out = captureStdout(() => {
      measureReload({ saveTs: 100, visibleTs: 600 });
    });
    const lines = out.trim().split('\n');
    assert.equal(lines.length, 1);
    const payload = JSON.parse(lines[0]);
    assert.deepEqual(Object.keys(payload).sort(), [
      'budgetMs',
      'elapsedMs',
      'withinBudget',
    ]);
    assert.equal(payload.elapsedMs, 500);
    assert.equal(payload.budgetMs, BUDGET_MS);
    assert.equal(payload.withinBudget, 500 <= BUDGET_MS);
  });

  it('throws RangeError for inverted marks (visible before save)', () => {
    assert.throws(
      () => measureReload({ saveTs: 1000, visibleTs: 999 }),
      RangeError,
    );
  });

  it('throws TypeError for non-numeric marks', () => {
    assert.throws(
      () => measureReload({ saveTs: 'now', visibleTs: 100 }),
      TypeError,
    );
    assert.throws(
      () => measureReload({ saveTs: 0, visibleTs: Number.NaN }),
      TypeError,
    );
  });

  it('consumes the budget via import from the perf manifest (never defines it)', () => {
    const source = fs.readFileSync(
      path.join(__dirname, '..', 'run.js'),
      'utf8',
    );
    assert.match(source, /budgets\.json/);
    assert.doesNotMatch(source, /(BUDGET|budgetMs)\s*=\s*\d/);
  });
});
