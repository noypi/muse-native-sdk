'use strict';
// Fault injection + throttling session config (FR-DX-009 / AC-DX-041).
// Applies per-session profiles + REL-RESILIENCE fault hooks to the engine's
// own transport stack through a test seam only: an in-memory double
// injected via the `transport` field or the `__setTransport` hook.
// Faults are recorded on the double, never executed against real
// infrastructure. Deterministic: no Math.random, no clock, no network.

const { validateProfile, normalizeProfile } = require('./throttling');

let hookedTransport = null;

function __setTransport(double) {
  hookedTransport = double || null;
}

function __getTransport() {
  return hookedTransport;
}

function fail(cause) {
  const err = new Error(cause);
  err.command = 'configureSession';
  err.cause = cause;
  err.action = 'Fix the configureSession arguments (profile kind + ms/lossPct, fault target/kind/rate in 0..1) and retry.';
  throw err;
}

function validateFaults(faults) {
  if (faults === undefined) return [];
  if (!Array.isArray(faults)) fail('invalid faults: expected an array');
  for (const fault of faults) {
    if (!fault || typeof fault !== 'object') fail('invalid fault: expected {target, kind, rate}');
    if (typeof fault.target !== 'string' || fault.target.length === 0) {
      fail('invalid fault: missing target (non-empty string required)');
    }
    if (typeof fault.kind !== 'string' || fault.kind.length === 0) {
      fail('invalid fault: missing kind (non-empty string required)');
    }
    if (typeof fault.rate !== 'number' || !Number.isFinite(fault.rate) || fault.rate < 0 || fault.rate > 1) {
      fail(`invalid fault rate "${fault && fault.rate}": expected a finite number in 0..1`);
    }
  }
  return faults;
}

function resolveTransport(opts) {
  if (opts && opts.transport) return opts.transport;
  return hookedTransport;
}

function applyToTransport(double, profile, faults) {
  if (!double) return;
  if (typeof double.applyProfile === 'function') {
    double.applyProfile({ ...profile });
  } else {
    double.appliedProfile = { ...profile }; // eslint-disable-line no-param-reassign
  }
  const copies = faults.map((f) => ({ ...f }));
  if (typeof double.applyFaults === 'function') {
    double.applyFaults(copies);
  } else {
    double.appliedFaults = copies.map((f) => ({ ...f })); // eslint-disable-line no-param-reassign
  }
}

function configureSession(opts) {
  const options = opts || {};
  validateProfile(options.profile);
  const faults = validateFaults(options.faults);
  const profile = normalizeProfile(options.profile);
  const faultCopies = faults.map((f) => ({ ...f }));
  // Validate everything before touching the double: invalid combos
  // reject with the structured error and never configure.
  const double = resolveTransport(options);
  applyToTransport(double, profile, faultCopies);
  return { profile: { ...profile }, faults: faultCopies.map((f) => ({ ...f })) };
}

module.exports = { configureSession, __setTransport, __getTransport };
