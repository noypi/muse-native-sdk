'use strict';

// T7 — per-artifact size gate against perf footprint budgets
// (NFR-BUILD-010 / AC-BUILD-029, required dep AC-BUILD-072).
// Budgets are caller input (imported from the perf reference by the caller,
// never redefined here). Fail closed: every artifact must match a budget by
// name; unbudgeted artifacts are overruns.

function checkSizes(artifacts, budgets) {
  const list = Array.isArray(artifacts) ? artifacts : [];
  const limits = Array.isArray(budgets) ? budgets : [];
  const byName = new Map(limits.map((b) => [b.name, b.bytes]));
  const overruns = [];
  for (const artifact of list) {
    const name = artifact && typeof artifact === 'object' && artifact.name !== undefined ? artifact.name : String(artifact);
    const bytes = artifact && typeof artifact === 'object' ? artifact.bytes : undefined;
    if (typeof bytes !== 'number' || !Number.isFinite(bytes) || bytes < 0) {
      overruns.push(
        `${name}: invalid size (${String(bytes)} bytes); treated as overrun`
      );
      continue;
    }
    if (!byName.has(artifact.name)) {
      overruns.push(
        `${artifact.name}: no budget (unbudgeted, ${artifact.bytes} bytes)`
      );
      continue;
    }
    const budget = byName.get(artifact.name);
    if (typeof budget !== 'number' || !Number.isFinite(budget) || budget < 0) {
      overruns.push(
        `${name}: invalid budget (${String(budget)} bytes); treated as overrun`
      );
      continue;
    }
    if (artifact.bytes > budget) {
      const delta = artifact.bytes - budget;
      overruns.push(
        `${artifact.name}: over budget by ${delta} bytes (${artifact.bytes} > ${budget})`
      );
    }
  }
  return { ok: overruns.length === 0, overruns };
}

module.exports = { checkSizes };
