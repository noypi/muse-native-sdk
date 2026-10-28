'use strict';

// T9 — Minified release must pass the full TEST-002 matrix; shrink breakages block
// (NFR-BUILD-007 / AC-BUILD-013; required dep TEST-002 — result rows consumed,
// matrix defined elsewhere, never imported here).

// Shrink-breakage evidence: any signal mentioning NoSuchMethodError,
// ClassNotFound, or shrink blocks, even on a row that claims passed.
const SHRINK_SIGNAL = /nosuchmethoderror|classnotfound|shrink/i;

function matrixVerdict(results) {
  if (!Array.isArray(results)) {
    return { ok: false, blockers: [`matrix: expected array of TEST-002 rows (got ${typeof results})`] };
  }
  if (results.length === 0) {
    return { ok: false, blockers: ['matrix: empty TEST-002 matrix proves nothing'] };
  }
  const blockers = [];
  results.forEach((row, index) => {
    if (row === null || typeof row !== 'object' || Array.isArray(row)) {
      blockers.push(`row[${index}]: malformed TEST-002 row, expected {suite, passed}`);
      return;
    }
    const label = typeof row.suite === 'string' && row.suite.length > 0
      ? row.suite
      : `row[${index}]`;
    if (typeof row.suite !== 'string' || row.suite.length === 0) {
      blockers.push(`${label}: missing suite name, cannot trust minified matrix row`);
      return;
    }
    if (typeof row.passed !== 'boolean') {
      blockers.push(`${row.suite}: malformed passed flag, expected boolean`);
      return;
    }
    if (row.signal !== undefined && row.signal !== null && typeof row.signal !== 'string') {
      blockers.push(`${row.suite}: malformed shrink signal, expected string`);
      return;
    }
    const signal = typeof row.signal === 'string' ? row.signal : '';
    if (signal.length > 0 && SHRINK_SIGNAL.test(signal)) {
      blockers.push(`${row.suite}: shrink-breakage signal blocks minified release (${signal})`);
      return;
    }
    if (row.passed !== true) {
      blockers.push(`${row.suite}: suite failed, minified release blocked`);
    }
  });
  return { ok: blockers.length === 0, blockers };
}

module.exports = { matrixVerdict };
