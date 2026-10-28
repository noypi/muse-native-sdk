'use strict';
// Throttling profiles for the debug engine transport (FR-DX-009 / AC-DX-041).
// Validates + normalizes per-session profiles. Applied to the engine's own
// transport stack via a seam (see ./fault-injection); no transport
// internals are built here.

const VALID_KINDS = ['offline', 'highLatency', 'lossy'];

function fail(cause) {
  const err = new Error(cause);
  err.command = 'configureSession';
  err.cause = cause;
  err.action = 'Fix the session profile (kind offline|highLatency|lossy with required ms/lossPct) and retry configureSession.';
  throw err;
}

function validateProfile(profile) {
  if (!profile || typeof profile !== 'object') {
    fail('missing profile: provide {kind: offline|highLatency|lossy}');
  }
  if (!VALID_KINDS.includes(profile.kind)) {
    fail(`unknown profile kind "${profile && profile.kind}": expected one of ${VALID_KINDS.join('|')}`);
  }
  if (profile.kind === 'highLatency') {
    if (profile.ms === undefined || profile.ms === null) {
      fail('highLatency profile requires numeric ms >= 0');
    }
    if (typeof profile.ms !== 'number' || !Number.isFinite(profile.ms) || profile.ms < 0) {
      fail(`invalid highLatency ms "${profile.ms}": expected a finite number >= 0`);
    }
  }
  if (profile.kind === 'lossy') {
    if (profile.lossPct === undefined || profile.lossPct === null) {
      fail('lossy profile requires numeric lossPct in 0..100');
    }
    if (typeof profile.lossPct !== 'number' || !Number.isFinite(profile.lossPct) || profile.lossPct < 0 || profile.lossPct > 100) {
      fail(`invalid lossy lossPct "${profile.lossPct}": expected a finite number in 0..100`);
    }
  }
}

function normalizeProfile(profile) {
  const out = { kind: profile.kind };
  if (profile.kind === 'highLatency') out.ms = profile.ms;
  if (profile.kind === 'lossy') out.lossPct = profile.lossPct;
  return out;
}

module.exports = { VALID_KINDS, validateProfile, normalizeProfile };
