'use strict';
// T12 — per-flavor environment injection gate (NFR-BUILD-018 / AC-BUILD-069).
// Fails closed: any production secret in the committed set is a violation
// naming the key; any required key left unresolved (missing/empty) is a
// violation naming the key; a missing descriptor fails closed, never throws.

const REQUIRED_KEYS = ['API_ENDPOINT', 'API_KEY'];

function verifyEnvInjection(flavor) {
  try {
    if (!flavor || typeof flavor !== 'object') {
      return { ok: false, violations: ['missing flavor descriptor: cannot verify env injection'] };
    }
    const violations = [];
    const values = flavor.values && typeof flavor.values === 'object' ? flavor.values : {};
    for (const key of REQUIRED_KEYS) {
      const value = values[key];
      if (typeof value !== 'string' || value.trim().length === 0) {
        violations.push(`unresolved required key: '${key}'`);
      }
    }
    const committed = flavor.committedSecrets;
    if (Array.isArray(committed)) {
      for (const secret of committed) {
        violations.push(`committed production secret: '${String(secret)}'`);
      }
    } else if (committed !== undefined) {
      violations.push(`invalid committedSecrets: '${String(committed)}'`);
    }
    return { ok: violations.length === 0, violations };
  } catch (err) {
    return { ok: false, violations: [`verification failed: '${err && err.message ? err.message : String(err)}'`] };
  }
}

module.exports = { verifyEnvInjection };
