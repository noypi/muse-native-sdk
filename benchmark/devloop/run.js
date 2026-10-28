'use strict';
// T4 — FR-DX-002 (AC-DX-007): dev-loop reload-time budget harness.
// The reload budget threshold is perf-owned and lives in the canonical manifest
// at `benchmark/budgets.json` (numeric `reloadBudgetMs`). This file imports it
// and never defines it — see loadBudgetMs() below.
//
// The manifest exists at `benchmark/budgets.json` (borrowed, perf-owned);
// the threshold is always read from there.

const budgets = require('../budgets.json');

function loadBudgetMs() {
  return budgets.reloadBudgetMs;
}

function measureReload(marks) {
  const { saveTs, visibleTs } = marks ?? {};
  if (typeof saveTs !== 'number' || Number.isNaN(saveTs)) {
    throw new TypeError('saveTs must be a non-NaN number');
  }
  if (typeof visibleTs !== 'number' || Number.isNaN(visibleTs)) {
    throw new TypeError('visibleTs must be a non-NaN number');
  }
  if (visibleTs < saveTs) {
    throw new RangeError('visibleTs must not precede saveTs');
  }
  const elapsedMs = visibleTs - saveTs;
  const budget = loadBudgetMs();
  const withinBudget = elapsedMs <= budget;
  process.stdout.write(
    `${JSON.stringify({ elapsedMs, budgetMs: budget, withinBudget })}\n`,
  );
  return { elapsedMs, withinBudget };
}

module.exports = { measureReload };
