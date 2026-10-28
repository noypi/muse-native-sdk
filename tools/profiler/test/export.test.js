'use strict';
// T8 — FR-DX-004 / AC-DX-017 / TC-DX-017 (Batch 4).
// Hermetic: node:test + node:assert/strict only. The session is a plain
// object built inline (frames in the T6 shape, snapshots in the T7 shape);
// no device, no Perfetto binary, no network, no clock, no config.
// Interface under test (plan verbatim):
//   exportSession(session, deviceInfo) => { trace: { format: 'perfetto-json-v1', events }, manifest: { device, os, engineVersion, sessionId } }
// Session shape under test (ASSUMPTION, see contract):
//   { sessionId, frames?: Array<{ passes: [{ name, subsystem, costMs }] }>, snapshots?: Array<object> }
// Manifest rule (per plan Detail): every field of
//   deviceInfo = { device, os, engineVersion, sessionId }
// is required — a missing field throws { code: 'E_PROFILER_BAD_MANIFEST', ... }.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'export.js');

function load() {
  return require(LIB);
}

function session(sessionId) {
  return {
    sessionId,
    frames: [{ passes: [{ name: 'render', subsystem: 'engine', costMs: 24 }] }],
    snapshots: [{ engineBytes: 10, jsHeapBytes: 50, gcEvents: 2, vramBytes: 10, nativeHeapBytes: 10 }],
  };
}

function deviceInfo(sessionId) {
  return { device: 'test-device', os: 'test-os-1.0', engineVersion: '0.0.0-test', sessionId };
}

function assertNoLeak(err) {
  assert.ok(err instanceof Error, 'must throw an Error');
  assert.doesNotMatch(String(err.stack ?? '').split('\n').slice(1).join('\n'), /node_modules/);
  return true;
}

describe('exportSession trace', () => {
  it('exports a mixed session to a perfetto-json-v1 trace with events', () => {
    const { exportSession } = load();
    const out = exportSession(session('s1'), deviceInfo('s1'));
    assert.equal(out.trace.format, 'perfetto-json-v1');
    assert.ok(Array.isArray(out.trace.events), 'events must be an array');
    assert.ok(out.trace.events.length > 0, 'mixed session must yield events');
  });

  it('yields a JSON-serializable trace (parseable round-trip)', () => {
    const { exportSession } = load();
    const out = exportSession(session('s1'), deviceInfo('s1'));
    const roundTrip = JSON.parse(JSON.stringify(out.trace));
    assert.equal(roundTrip.format, 'perfetto-json-v1');
    assert.ok(Array.isArray(roundTrip.events));
  });

  it('exports an empty session to a valid empty trace', () => {
    const { exportSession } = load();
    const out = exportSession({ sessionId: 's-empty' }, deviceInfo('s-empty'));
    assert.equal(out.trace.format, 'perfetto-json-v1');
    assert.ok(Array.isArray(out.trace.events));
  });

  it('rejects a malformed session without leaking internals (ASSUMPTION)', () => {
    const { exportSession } = load();
    for (const bad of [null, undefined, 's1', 42, {}]) {
      assert.throws(() => exportSession(bad, deviceInfo('s1')), assertNoLeak);
    }
  });
});

describe('exportSession manifest', () => {
  it('returns a complete manifest attachable to a bug report', () => {
    const { exportSession } = load();
    const out = exportSession(session('s1'), deviceInfo('s1'));
    assert.equal(out.manifest.device, 'test-device');
    assert.equal(out.manifest.os, 'test-os-1.0');
    assert.equal(out.manifest.engineVersion, '0.0.0-test');
    assert.equal(out.manifest.sessionId, 's1');
  });

  it('throws E_PROFILER_BAD_MANIFEST when device is missing', () => {
    const { exportSession } = load();
    const info = deviceInfo('s1');
    delete info.device;
    assert.throws(() => exportSession(session('s1'), info), (err) => {
      assert.equal(err.code, 'E_PROFILER_BAD_MANIFEST');
      assertNoLeak(err);
      return true;
    });
  });

  it('throws E_PROFILER_BAD_MANIFEST when os is missing', () => {
    const { exportSession } = load();
    const info = deviceInfo('s1');
    delete info.os;
    assert.throws(() => exportSession(session('s1'), info), (err) => {
      assert.equal(err.code, 'E_PROFILER_BAD_MANIFEST');
      assertNoLeak(err);
      return true;
    });
  });

  it('throws E_PROFILER_BAD_MANIFEST when engineVersion is missing', () => {
    const { exportSession } = load();
    const info = deviceInfo('s1');
    delete info.engineVersion;
    assert.throws(() => exportSession(session('s1'), info), (err) => {
      assert.equal(err.code, 'E_PROFILER_BAD_MANIFEST');
      assertNoLeak(err);
      return true;
    });
  });

  it('throws E_PROFILER_BAD_MANIFEST when sessionId is missing', () => {
    const { exportSession } = load();
    const info = deviceInfo('s1');
    delete info.sessionId;
    assert.throws(() => exportSession(session('s1'), info), (err) => {
      assert.equal(err.code, 'E_PROFILER_BAD_MANIFEST');
      assertNoLeak(err);
      return true;
    });
  });

  it('throws E_PROFILER_BAD_MANIFEST when deviceInfo is absent', () => {
    const { exportSession } = load();
    for (const bad of [null, undefined, {}]) {
      assert.throws(() => exportSession(session('s1'), bad), (err) => {
        assert.equal(err.code, 'E_PROFILER_BAD_MANIFEST');
        assertNoLeak(err);
        return true;
      });
    }
  });
});
