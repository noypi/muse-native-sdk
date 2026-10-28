'use strict';
// T5 — Versioned snapshot schema with migration and safe discard.
// Current version SNAPSHOT_VERSION = 2; unknown/corrupt/older-than-supported
// snapshots discard safely without throwing.

const SNAPSHOT_VERSION = 2;

function isPlainRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function wrapSnapshot(payload, version) {
  const envelopeVersion = typeof version === 'undefined' ? SNAPSHOT_VERSION : version;
  return { version: envelopeVersion, payload };
}

function unwrapSnapshot(raw, migrations) {
  try {
    if (!isPlainRecord(raw)) {
      return { status: 'discarded', payload: null };
    }
    const version = raw.version;
    const payload = raw.payload;
    if (!Number.isInteger(version) || version <= 0) {
      return { status: 'discarded', payload: null };
    }
    if (!isPlainRecord(payload)) {
      return { status: 'discarded', payload: null };
    }
    if (version === SNAPSHOT_VERSION) {
      return { status: 'current', payload };
    }
    if (version > SNAPSHOT_VERSION) {
      return { status: 'discarded', payload: null };
    }
    // Older-than-current: migrate only via a caller-supplied entry.
    const migrateFn =
      migrations && typeof migrations === 'object' ? migrations[version] : undefined;
    if (typeof migrateFn !== 'function') {
      return { status: 'discarded', payload: null };
    }
    const migrated = migrateFn(payload);
    if (!isPlainRecord(migrated)) {
      return { status: 'discarded', payload: null };
    }
    return { status: 'migrated', payload: migrated };
  } catch (_err) {
    return { status: 'discarded', payload: null };
  }
}

module.exports = { wrapSnapshot, unwrapSnapshot, SNAPSHOT_VERSION };
