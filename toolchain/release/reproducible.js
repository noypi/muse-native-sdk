'use strict';

const crypto = require('node:crypto');

// T1 — deterministic release artifact normalization + digest
// (AC-BUILD-002). Pure functions; fail closed with TypeError on
// non-array input or malformed entries. Volatile metadata (timestamps,
// uid/gid, ordering) arrives as extra props and is stripped: output
// entries carry only `{ path, bytes }` with freshly copied buffers.

function assertEntry(entry) {
  if (!entry || typeof entry !== 'object') {
    throw new TypeError('entry must be an object with { path, bytes }');
  }
  if (typeof entry.path !== 'string') {
    throw new TypeError('entry.path must be a string');
  }
  if (!Buffer.isBuffer(entry.bytes)) {
    throw new TypeError('entry.bytes must be a Buffer');
  }
}

function comparePathBytes(a, b) {
  return Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
}

function normalizeReleaseArtifact(entries) {
  if (!Array.isArray(entries)) {
    throw new TypeError('entries must be an array');
  }
  const cleaned = entries.map((entry) => {
    assertEntry(entry);
    return { path: entry.path, bytes: Buffer.from(entry.bytes) };
  });
  cleaned.sort((a, b) => comparePathBytes(a.path, b.path));
  return cleaned;
}

function hashArtifact(normalized) {
  if (!Array.isArray(normalized)) {
    throw new TypeError('normalized must be an array');
  }
  const hash = crypto.createHash('sha256');
  const count = Buffer.allocUnsafe(4);
  count.writeUInt32BE(normalized.length, 0);
  hash.update(count);
  for (const entry of normalized) {
    assertEntry(entry);
    const pathBytes = Buffer.from(entry.path, 'utf8');
    const pathLen = Buffer.allocUnsafe(4);
    pathLen.writeUInt32BE(pathBytes.length, 0);
    const contentLen = Buffer.allocUnsafe(4);
    contentLen.writeUInt32BE(entry.bytes.length, 0);
    hash.update(pathLen);
    hash.update(pathBytes);
    hash.update(contentLen);
    hash.update(entry.bytes);
  }
  return hash.digest('hex');
}

module.exports = { normalizeReleaseArtifact, hashArtifact };
