'use strict';
// Profiler Perfetto export + bug-report manifest (FR-DX-004 / AC-DX-017).
// Pure in-memory serialization of T6 frames + T7 snapshots into
// Perfetto-JSON-shaped events. No Perfetto binary, no device, no clock.

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function malformedError(detail) {
  const err = new Error(`malformed profiler session (DX-004 / AC-DX-017): ${detail}`);
  err.code = 'E_PROFILER_MALFORMED_SESSION';
  err.cause = detail;
  err.action = 'Pass a session object shaped { sessionId, frames?, snapshots? }.';
  return err;
}

function manifestError(detail) {
  const err = new Error(`incomplete profiler manifest (DX-004 / AC-DX-017): ${detail}`);
  err.code = 'E_PROFILER_BAD_MANIFEST';
  err.cause = detail;
  err.action = 'Provide deviceInfo shaped { device, os, engineVersion, sessionId }.';
  return err;
}

function validateSession(session) {
  if (!isPlainObject(session)) throw malformedError('session must be an object');
  if (typeof session.sessionId !== 'string' || session.sessionId.length === 0) {
    throw malformedError('session.sessionId must be a non-empty string');
  }
  for (const key of ['frames', 'snapshots']) {
    if (session[key] !== undefined && !Array.isArray(session[key])) {
      throw malformedError(`session.${key} must be an array when present`);
    }
  }
}

function validateManifest(deviceInfo) {
  if (!isPlainObject(deviceInfo)) throw manifestError('deviceInfo must be an object');
  for (const field of ['device', 'os', 'engineVersion', 'sessionId']) {
    if (typeof deviceInfo[field] !== 'string' || deviceInfo[field].length === 0) {
      throw manifestError(`${field} must be a non-empty string`);
    }
  }
}

function toFiniteNumber(value, fallback) {
  return typeof value === 'number' && Number.isFinite(value) && value >= 0 ? value : fallback;
}

function exportSession(session, deviceInfo) {
  validateSession(session);
  validateManifest(deviceInfo);

  const frames = session.frames ?? [];
  const snapshots = session.snapshots ?? [];
  const events = [];

  frames.forEach((frame, frameIndex) => {
    const passes = frame && Array.isArray(frame.passes) ? frame.passes : [];
    passes.forEach((pass, passIndex) => {
      const name = pass && typeof pass.name === 'string' ? pass.name : `pass-${passIndex}`;
      const subsystem = pass && typeof pass.subsystem === 'string' ? pass.subsystem : 'engine';
      events.push({
        name,
        cat: subsystem,
        ph: 'X',
        ts: frameIndex * 1000 + passIndex,
        dur: toFiniteNumber(pass ? pass.costMs : undefined, 0),
        pid: 1,
        tid: 1,
        args: { frame: frameIndex, subsystem },
      });
    });
  });

  snapshots.forEach((snapshot, snapshotIndex) => {
    const args = {};
    if (isPlainObject(snapshot)) {
      for (const field of ['engineBytes', 'jsHeapBytes', 'vramBytes', 'nativeHeapBytes']) {
        if (typeof snapshot[field] === 'number' && Number.isFinite(snapshot[field])) {
          args[field] = snapshot[field];
        }
      }
    }
    events.push({
      name: 'memory',
      ph: 'C',
      ts: (frames.length + snapshotIndex) * 1000,
      pid: 1,
      args,
    });
  });

  return {
    trace: { format: 'perfetto-json-v1', events },
    manifest: {
      device: deviceInfo.device,
      os: deviceInfo.os,
      engineVersion: deviceInfo.engineVersion,
      sessionId: deviceInfo.sessionId,
    },
  };
}

module.exports = { exportSession };
