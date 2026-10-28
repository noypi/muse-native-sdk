'use strict';

// T10 — mapping.txt present in release artifacts for symbolication
// (NFR-BUILD-007 / AC-BUILD-014). Reliability symbolication consumes mapping.txt;
// this module only proves presence (exact artifact path, non-empty).

function mappingPresent(entry) {
  if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) {
    return { ok: false, reason: 'entry: missing mapping entry cannot prove symbolication artifact' };
  }
  if (entry.path !== 'dist/mapping.txt') {
    return { ok: false, reason: `path: expected 'dist/mapping.txt' (got ${JSON.stringify(entry.path)})` };
  }
  if (typeof entry.bytes !== 'number' || !Number.isFinite(entry.bytes) || entry.bytes <= 0) {
    return { ok: false, reason: `bytes: expected finite number > 0 (got ${JSON.stringify(entry.bytes)})` };
  }
  return { ok: true, reason: 'mapping present at dist/mapping.txt' };
}

module.exports = { mappingPresent };
