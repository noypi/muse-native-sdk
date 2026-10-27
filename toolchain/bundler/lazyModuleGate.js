'use strict';

// T3 — lazy-module / first-frame gate (NFR-BUILD-012 / AC-BUILD-038).
// Pure manifest comparison: stdlib-only, no engine import, no I/O.

function checkLazyModules(input) {
  if (input === null || typeof input !== 'object') {
    throw new TypeError('checkLazyModules expects an argument object');
  }
  const { startupModules, lazyManifest } = input;
  if (!Array.isArray(startupModules)) {
    throw new TypeError('startupModules must be an array');
  }
  if (!Array.isArray(lazyManifest)) {
    throw new TypeError('lazyManifest must be an array');
  }

  const allowed = new Set(startupModules);
  const violations = [];
  const seen = new Set();

  for (const entry of lazyManifest) {
    if (entry === null || typeof entry !== 'object') {
      throw new TypeError('lazyManifest entries must be objects');
    }
    if (typeof entry.module !== 'string') {
      throw new TypeError('lazyManifest entry.module must be a string');
    }
    if (entry.loadedAt !== 'startup' && entry.loadedAt !== 'lazy') {
      throw new TypeError(`unknown loadedAt value: ${String(entry.loadedAt)}`);
    }
    if (entry.loadedAt === 'startup' && !allowed.has(entry.module) && !seen.has(entry.module)) {
      seen.add(entry.module);
      violations.push(entry.module);
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { checkLazyModules };
