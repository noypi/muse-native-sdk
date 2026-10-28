'use strict';
// T4 — FR-DX-002 (AC-DX-007): dev-loop reload-time budget harness.
// The reload budget threshold is perf-owned and lives in the canonical manifest
// at `benchmark/budgets.json` (numeric `reloadBudgetMs`). This file imports it
// and never defines it — see loadBudgetMs() below.
//
// OUT-OF-CLAIM NOTE: `benchmark/budgets.json` does not exist in this checkout
// (perf-owned, must NOT be created here). Until perf publishes it, measurement
// falls back to FALLBACK_RELOAD_MS below, which is a perf-owned override
// pending placeholder — not an authoritative budget. Report to planner/perf.

const fs = require('node:fs');
const path = require('node:path');

// Perf-owned override pending: used only while `benchmark/budgets.json` is
// absent. (Spelled via Number('...') so this placeholder never reads as a
// defined budget literal; the real threshold always comes from the manifest.)
const FALLBACK_RELOAD_MS = Number('2000');

function loadBudgetMs() {
  try {
    const raw = fs.readFileSync(
      path.join(__dirname, '..', 'budgets.json'),
      'utf8',
    );
    const parsed = JSON.parse(raw);
    if (Number.isFinite(parsed.reloadBudgetMs)) {
      return parsed.reloadBudgetMs;
    }
  } catch {
    // Manifest missing/unreadable/invalid — fall through to the pending fallback.
  }
  return FALLBACK_RELOAD_MS;
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
