'use strict';
// T10 — Performance gate (NFR-BUILD-015 / AC-BUILD-054).
// Fails closed: any failed suite blocks, and empty results block.

function performanceGate(results) {
  if (!Array.isArray(results) || results.length === 0) {
    return { ok: false, failures: ['no performance results: failing closed'] };
  }
  const failures = [];
  for (const entry of results) {
    if (!entry || entry.passed !== true) {
      const suite = entry && entry.suite ? String(entry.suite) : 'unknown suite';
      failures.push(`performance budget failed: ${suite}`);
    }
  }
  return { ok: failures.length === 0, failures };
}

module.exports = { performanceGate };
