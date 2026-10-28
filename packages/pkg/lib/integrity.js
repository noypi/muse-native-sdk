'use strict';

// Hash-only integrity gate (Q-PKG-001 hash-only MVP, no signature field).
// `verifyIntegrity` compares the sha256 of every archive file against the
// recorded hashes from `integrity.json` and throws before any native open.

const crypto = require('node:crypto');

const { PkgError } = require('./errors.js');

function sha256Hex(content) {
  const buf = Buffer.isBuffer(content) ? content : Buffer.from(content);
  return crypto.createHash('sha256').update(buf).digest('hex');
}

function verifyIntegrity(files, integrity) {
  const record = integrity && typeof integrity === 'object' ? integrity : {};
  for (const [filePath, content] of files) {
    const expected = record[filePath];
    if (typeof expected !== 'string' || expected.length === 0) {
      throw new PkgError(
        'E_INTEGRITY',
        filePath,
        `sha256 of "${filePath}" listed in integrity.json`,
      );
    }
    if (sha256Hex(content) !== expected) {
      // The error carries the recorded hash only — never the file bytes.
      throw new PkgError('E_INTEGRITY', filePath, expected);
    }
  }
}

module.exports = { verifyIntegrity, sha256Hex };
