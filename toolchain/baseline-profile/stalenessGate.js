'use strict';

// T4 — Profile staleness gate: committed profile must not predate release sources
// (NFR-BUILD-006 / AC-BUILD-008).
// A stale profile (sourceStamp older than the release sources stamp) or an
// unverifiable profile (missing/empty hash, missing/non-numeric stamps) fails
// closed. Regenerate a fresh profile with regenerate(), which returns the one
// CLI command string: "./gradlew :app:generateBaselineProfile".

const REGENERATE_COMMAND = './gradlew :app:generateBaselineProfile';

function stalenessCheck(profile, sources) {
  const entry = profile !== null && typeof profile === 'object' ? profile : {};
  const origin = sources !== null && typeof sources === 'object' ? sources : {};
  if (typeof entry.hash !== 'string' || entry.hash.trim().length === 0) {
    return { ok: false, reason: 'missing or empty profile hash' };
  }
  if (
    typeof entry.sourceStamp !== 'number' ||
    !Number.isFinite(entry.sourceStamp)
  ) {
    return {
      ok: false,
      reason: `invalid profile sourceStamp (${String(entry.sourceStamp)}); treated as stale`,
    };
  }
  if (typeof origin.stamp !== 'number' || !Number.isFinite(origin.stamp)) {
    return {
      ok: false,
      reason: `invalid sources stamp (${String(origin.stamp)}); treated as stale`,
    };
  }
  if (entry.sourceStamp < origin.stamp) {
    return {
      ok: false,
      reason: `stale profile: sourceStamp ${entry.sourceStamp} older than sources stamp ${origin.stamp}`,
    };
  }
  return { ok: true, reason: 'profile is fresh' };
}

function regenerate() {
  return REGENERATE_COMMAND;
}

module.exports = { stalenessCheck, regenerate };
