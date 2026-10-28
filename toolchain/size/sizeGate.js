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

// T8 — size gate over T5 report rows (NFR-BUILD-019 / AC-BUILD-075).
// Input shape mirrors recordSizes rows by reference ({ name, installBytes,
// budget }); budgets remain caller input, never redefined here. Fail closed:
// non-finite/negative installBytes is an overrun naming the artifact.
function checkSizeGate(report) {
  if (!report || typeof report !== 'object' || !Array.isArray(report.rows)) {
    return { pass: false, deltas: [{ name: '<report>', delta: NaN }] };
  }
  const rows = report.rows;
  const deltas = [];
  for (const row of rows) {
    const name =
      row && typeof row === 'object' && row.name !== undefined
        ? row.name
        : String(row);
    const installBytes =
      row && typeof row === 'object' ? row.installBytes : undefined;
    const budget = row && typeof row === 'object' ? row.budget : undefined;
    if (
      typeof installBytes !== 'number' ||
      !Number.isFinite(installBytes) ||
      installBytes < 0
    ) {
      deltas.push({
        name,
        delta: Number.isFinite(installBytes) ? installBytes : NaN,
      });
      continue;
    }
    if (
      typeof budget !== 'number' ||
      !Number.isFinite(budget) ||
      budget < 0
    ) {
      deltas.push({ name, delta: NaN });
      continue;
    }
    if (installBytes > budget) {
      deltas.push({ name, delta: installBytes - budget });
    }
  }
  return { pass: deltas.length === 0, deltas };
}

module.exports = { checkSizes, checkSizeGate };
