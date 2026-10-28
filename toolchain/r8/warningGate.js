'use strict';

// T11 — Minified-release accept: zero R8 warnings + matrix green + mapping present
// (NFR-BUILD-007 / AC-BUILD-015; composes T9 + T10 plus warning scan — all inputs
// are caller-supplied values, never imports of T9/T10 modules).

// Only these warning kinds block release; informational kinds are ignored.
const BLOCKER_KINDS = new Set(['missing-class', 'invalid-keep']);

// Shrink-breakage evidence: mirrors T9 (any such signal blocks, even on a row
// that claims passed).
const SHRINK_SIGNAL = /nosuchmethoderror|classnotfound|shrink/i;

function releaseAccept(warnings, matrix, mapping) {
  const reasons = [];

  if (!Array.isArray(warnings)) {
    reasons.push(`warnings: expected array of {kind, count} rows (got ${typeof warnings})`);
  } else {
    warnings.forEach((entry, index) => {
      if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) {
        reasons.push(`warnings: malformed entry at index ${index}, expected {kind, count}`);
        return;
      }
      if (!BLOCKER_KINDS.has(entry.kind)) {
        return;
      }
      if (typeof entry.count !== 'number' || !Number.isFinite(entry.count)) {
        reasons.push(`warnings: malformed entry for '${entry.kind}', expected finite count (got ${JSON.stringify(entry.count)})`);
        return;
      }
      if (entry.count > 0) {
        reasons.push(`${entry.kind}: R8 warning blocks minified release (count ${entry.count})`);
      }
    });
  }

  if (!Array.isArray(matrix)) {
    reasons.push(`matrix: expected array of TEST-002 rows (got ${typeof matrix})`);
  } else if (matrix.length === 0) {
    reasons.push('matrix: empty TEST-002 matrix proves nothing');
  } else {
    matrix.forEach((row, index) => {
      if (row === null || typeof row !== 'object' || Array.isArray(row)) {
        reasons.push(`matrix: row[${index}] malformed TEST-002 row, expected {suite, passed}`);
        return;
      }
      const label = typeof row.suite === 'string' && row.suite.length > 0
        ? row.suite
        : `matrix: row[${index}]`;
      if (typeof row.suite !== 'string' || row.suite.length === 0) {
        reasons.push(`${label}: missing suite name, cannot trust minified matrix row`);
        return;
      }
      if (typeof row.passed !== 'boolean') {
        reasons.push(`${row.suite}: matrix row malformed passed flag, expected boolean`);
        return;
      }
      if (row.signal !== undefined && row.signal !== null && typeof row.signal !== 'string') {
        reasons.push(`${row.suite}: malformed shrink signal, expected string`);
        return;
      }
      const signal = typeof row.signal === 'string' ? row.signal : '';
      if (signal.length > 0 && SHRINK_SIGNAL.test(signal)) {
        reasons.push(`${row.suite}: matrix shrink-breakage signal blocks minified release (${signal})`);
        return;
      }
      if (row.passed !== true) {
        reasons.push(`${row.suite}: matrix suite failed, minified release blocked`);
      }
    });
  }

  if (mapping === null || typeof mapping !== 'object' || Array.isArray(mapping)) {
    reasons.push('mapping: missing mapping entry cannot prove symbolication artifact');
  } else if (mapping.path !== 'dist/mapping.txt') {
    reasons.push(`mapping: expected path 'dist/mapping.txt' (got ${JSON.stringify(mapping.path)})`);
  } else if (typeof mapping.bytes !== 'number' || !Number.isFinite(mapping.bytes) || mapping.bytes <= 0) {
    reasons.push(`mapping: expected bytes > 0 (got ${JSON.stringify(mapping.bytes)})`);
  }

  return { ok: reasons.length === 0, reasons };
}

module.exports = { releaseAccept };
