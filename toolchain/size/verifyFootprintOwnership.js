'use strict';

// T1 — footprint-ownership gate: caller budgets must equal the perf reference
// (NFR-BUILD-004 / AC-BUILD-004; required dep AC-PERFORMANCE-009, reference
// values only, synthetic in tests).
// Budgets are caller input (imported from the perf reference by the caller,
// never redefined here). Pure comparison only — this module never writes
// thresholds and never touches the filesystem, network, or services.

function verifyOwnership(callerBudgets, perfReference) {
  if (!Array.isArray(callerBudgets) || !Array.isArray(perfReference)) {
    return { ok: false, violations: ['invalid input: callerBudgets and perfReference must be arrays'] };
  }
  const referenceByName = new Map();
  for (const entry of perfReference) {
    if (entry && typeof entry === 'object') {
      referenceByName.set(entry.name, entry.bytes);
    }
  }
  const violations = [];
  for (const caller of callerBudgets) {
    const name = caller && typeof caller === 'object' && caller.name !== undefined
      ? String(caller.name)
      : String(caller);
    const bytes = caller && typeof caller === 'object' ? caller.bytes : undefined;
    if (typeof bytes !== 'number' || !Number.isFinite(bytes) || bytes < 0) {
      violations.push(`${name}: invalid caller budget (${String(bytes)} bytes); treated as violation`);
      continue;
    }
    if (!referenceByName.has(caller.name)) {
      violations.push(`${name}: no perf reference entry (unbudgeted, ${bytes} bytes)`);
      continue;
    }
    const referenceBytes = referenceByName.get(caller.name);
    if (typeof referenceBytes !== 'number' || !Number.isFinite(referenceBytes) || referenceBytes < 0) {
      violations.push(`${name}: invalid perf reference (${String(referenceBytes)} bytes); treated as violation`);
      continue;
    }
    if (bytes !== referenceBytes) {
      violations.push(`${name}: caller budget differs from perf reference (${bytes} != ${referenceBytes})`);
    }
  }
  return { ok: violations.length === 0, violations };
}

module.exports = { verifyOwnership };
