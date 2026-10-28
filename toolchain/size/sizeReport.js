'use strict';
// T5 — Per-artifact size report vs referenced perf budgets (NFR-BUILD-019 / AC-BUILD-072, TC-BUILD-072).
// Hermetic: pure in-memory comparison only. Budget numbers are caller-supplied
// input imported from perf `binary_and_footprint_estimates` (never defined here);
// artifact taxonomy (base AAB, splits, asset packs, feature modules) follows
// BUILD-006 by reference.

class SizeError extends Error {
  constructor(code, message) {
    super(message || code);
    this.name = 'SizeError';
    this.code = code;
  }
}

function fail(code) {
  throw new SizeError(code, `SizeError('${code}')`);
}

function isValidSize(value) {
  return typeof value === 'number' && Number.isFinite(value) && value >= 0;
}

// recordSizes(artifacts, budgets): { rows, failures }
// - unknown artifact name (no key in budgets) throws SizeError('unknown-artifact').
// - over is true when EITHER downloadBytes OR installBytes exceeds the
//   single per-artifact budget; equal-to-budget is not over.
// - non-finite / negative / undefined sizes fail closed (over + named failure).
function recordSizes(artifacts, budgets) {
  const list = artifacts || [];
  const rows = [];
  const failures = [];
  for (const artifact of list) {
    const name = artifact && artifact.name;
    if (
      !budgets ||
      typeof budgets !== 'object' ||
      !Object.prototype.hasOwnProperty.call(budgets, name)
    ) {
      fail('unknown-artifact');
    }
    const budget = budgets[name];
    const { downloadBytes, installBytes } = artifact;
    const validDownload = isValidSize(downloadBytes);
    const validInstall = isValidSize(installBytes);
    let over = false;
    if (!validDownload || !validInstall) {
      over = true;
    } else if (downloadBytes > budget || installBytes > budget) {
      over = true;
    }
    rows.push({ name, downloadBytes, installBytes, budget, over });
    if (over) {
      const worst = Math.max(
        validDownload ? downloadBytes - budget : NaN,
        validInstall ? installBytes - budget : NaN
      );
      const delta = Number.isFinite(worst) && worst > 0 ? ` (+${worst} bytes over ${budget} budget)` : ' (invalid size)';
      failures.push(`${name} exceeds budget${delta}`);
    }
  }
  return { rows, failures };
}

module.exports = { recordSizes, SizeError };
