'use strict';
// T10 — FR-DX-009 / AC-DX-042 / AC-DX-043 + FR-DX-001 dev-channel exclusion
// (TC-DX-042 / TC-DX-043, Batch 4).
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. No device,
// no socket, no network, no secrets, no config. All collaborators are fakes.
// Interface under test (plan verbatim):
//   connect(buildType: 'dev'|'release') => ({status:'connected', session:
//   object} | {status:'refused', reason: "debug channel not compiled in",
//   action: string}) from tools/debugbridge/lib/release-stub.js; any inbound
//   debug connection to release is refused — a listener in release is a
//   blocking defect (AC-DX-042).
// ASSUMPTION (T1/T7/T8 committed): the Batch 3 seams
//   tools/debugbridge/lib/source-map-resolver.js (TS->bytecode) and
//   tools/debugbridge/lib/jsi-boundary.js (C++<->JSI) exist and are consumed
//   opaquely here via their committed interfaces; mapping fixtures are inline.
// ASSUMPTION: unknown/omitted buildType fails closed to `refused` (never
//   `connected`); the coder owns the exact error shape for that path.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const DEBUG_STUB = path.join(__dirname, '..', 'lib', 'release-stub.js');
const DEVSERVER_STUB = path.join(
  __dirname, '..', '..', 'devserver', 'lib', 'release-stub.js'
);
const SOURCE_MAP_RESOLVER = path.join(
  __dirname, '..', 'lib', 'source-map-resolver.js'
);
const JSI_BOUNDARY = path.join(__dirname, '..', 'lib', 'jsi-boundary.js');

function fresh(modPath) {
  delete require.cache[require.resolve(modPath)];
  return require(modPath);
}

function readSource(filePath) {
  return fs.readFileSync(filePath, 'utf8');
}

// Any listener creation in a release stub is a blocking defect (AC-DX-042).
const LISTENER_PATTERNS = [
  /\.listen\s*\(/,
  /createServer/,
  /createConnection/,
  /\.bind\s*\(/,
  /WebSocketServer/,
  /new\s+(?:net\.|dgram\.)?Server\b/,
  /app\.listen/,
  /Deno\.serve/,
  /Bun\.serve/,
];

// Release entry points must never require dev modules (A5 / NFR-DX-001).
const DEV_MODULE_PATTERNS = [
  /dev-channel-server/,
  /delta-applier/,
  /cdp-bridge/,
  /source-map-resolver/,
  /lldb-session/,
  /jsi-boundary/,
  /throttling/,
  /fault-injection/,
];

function assertNoListeners(label, source) {
  for (const re of LISTENER_PATTERNS) {
    assert.ok(
      !re.test(source),
      `${label}: blocking defect — release stub matches listener pattern ${re}`
    );
  }
}

function assertNoDevRequires(label, source) {
  for (const re of DEV_MODULE_PATTERNS) {
    assert.ok(
      !re.test(source),
      `${label}: release stub must never require dev module matching ${re}`
    );
  }
}

describe('release refusal (T10 / TC-DX-042)', () => {
  it('release refuses inbound debug connections', () => {
    const { connect } = fresh(DEBUG_STUB);
    assert.equal(typeof connect, 'function');
    const res = connect('release');
    assert.deepEqual(Object.keys(res).sort(), ['action', 'reason', 'status']);
    assert.equal(res.status, 'refused');
    assert.equal(res.reason, 'debug channel not compiled in');
    assert.equal(typeof res.action, 'string');
    assert.ok(res.action.length > 0, 'refusal must name the next action');
  });

  it('refusal never yields a session object', () => {
    const { connect } = fresh(DEBUG_STUB);
    const res = connect('release');
    assert.ok(!('session' in res), 'refused result must not carry a session');
  });

  it('unknown or omitted buildType fails closed (never connected)', () => {
    const { connect } = fresh(DEBUG_STUB);
    for (const bad of [undefined, null, 'prod', '', 42]) {
      let res = null;
      let err = null;
      try {
        res = connect(bad);
      } catch (e) {
        err = e;
      }
      if (err) {
        assert.ok(
          err && typeof err === 'object',
          'thrown refusal must be a structured error'
        );
      } else {
        assert.ok(
          res && res.status === 'refused',
          `buildType ${String(bad)} must refuse, got: ${JSON.stringify(res)}`
        );
        assert.equal(res.reason, 'debug channel not compiled in');
      }
    }
  });

  it('debugbridge release stub creates no listener (blocking defect scan)', () => {
    const source = readSource(DEBUG_STUB);
    assertNoListeners('tools/debugbridge/lib/release-stub.js', source);
  });

  it('devserver release stub creates no listener (blocking defect scan)', () => {
    const source = readSource(DEVSERVER_STUB);
    assertNoListeners('tools/devserver/lib/release-stub.js', source);
  });
});

describe('dev connects (T10 / FR-DX-001 dev channel)', () => {
  it('dev buildType connects with a session', () => {
    const { connect } = fresh(DEBUG_STUB);
    const res = connect('dev');
    assert.equal(res.status, 'connected');
    assert.ok(res.session && typeof res.session === 'object');
  });

  it('dev sessions are distinct objects (no shared mutable session)', () => {
    const { connect } = fresh(DEBUG_STUB);
    const a = connect('dev');
    const b = connect('dev');
    assert.equal(a.status, 'connected');
    assert.equal(b.status, 'connected');
    assert.ok(a.session !== b.session, 'each dev connect gets its own session');
  });
});

describe('release entry points never require dev modules (T10 / NFR-DX-001)', () => {
  it('debugbridge release stub requires no dev modules', () => {
    const source = readSource(DEBUG_STUB);
    assertNoDevRequires('tools/debugbridge/lib/release-stub.js', source);
  });

  it('devserver release stub requires no dev modules', () => {
    const source = readSource(DEVSERVER_STUB);
    assertNoDevRequires('tools/devserver/lib/release-stub.js', source);
  });
});

describe('TS breakpoint -> bytecode mapping via Batch 3 source-map seam (T10 / TC-DX-043)', () => {
  it('resolves a TS breakpoint to its generated bytecode location', () => {
    const seam = fresh(SOURCE_MAP_RESOLVER);
    assert.equal(typeof seam.createSourceMapResolver, 'function');
    const resolver = seam.createSourceMapResolver({
      'bundle.js:42': { source: 'src/App.tsx', line: 10, column: 3 },
    });
    const mapped = resolver.originalPositionFor({
      url: 'bundle.js',
      line: 42,
      column: 7,
    });
    assert.equal(mapped.source, 'src/App.tsx');
    assert.equal(mapped.line, 10);
  });

  it('maps a bytecode frame back to its original TS source', () => {
    const seam = fresh(SOURCE_MAP_RESOLVER);
    assert.equal(typeof seam.mapFrame, 'function');
    const resolver = seam.createSourceMapResolver({
      'bundle.js:42': { source: 'src/App.tsx', line: 10, column: 3 },
    });
    const frame = seam.mapFrame(
      { functionName: 'onPress', url: 'bundle.js', lineNumber: 42, columnNumber: 7 },
      resolver
    );
    const text = JSON.stringify(frame);
    assert.ok(
      /App\.tsx/.test(text),
      `mapped frame must name the TS source, got: ${text}`
    );
  });

  it('unmapped bytecode locations stay unmapped (never a half mapping)', () => {
    const seam = fresh(SOURCE_MAP_RESOLVER);
    const resolver = seam.createSourceMapResolver({});
    const frame = { functionName: 'f', url: 'vendor.js', lineNumber: 1, columnNumber: 0 };
    const out = seam.mapFrame(frame, resolver);
    assert.ok(out && typeof out === 'object');
    assert.ok(
      !('source' in out) || out.source == null || out.source === frame.url,
      'unmapped frame must not claim a TS source'
    );
  });
});

describe('C++ breakpoint -> originating JSI call via Batch 3 JSI map (T10 / TC-DX-043)', () => {
  it('resolves a C++ breakpoint to its originating JSI call', () => {
    const jsi = fresh(JSI_BOUNDARY);
    assert.equal(typeof jsi.stepAcrossJsi, 'function');
    const res = jsi.stepAcrossJsi('onPress');
    assert.equal(res.jsFunction, 'onPress');
    assert.equal(typeof res.nativeSymbol, 'string');
    assert.ok(res.nativeSymbol.length > 0);
    assert.ok(
      /jsi/i.test(res.nativeSymbol),
      `native symbol must name the JSI boundary, got: ${res.nativeSymbol}`
    );
  });

  it('steps back from the native symbol to the originating JS function', () => {
    const jsi = fresh(JSI_BOUNDARY);
    const fwd = jsi.stepAcrossJsi('onPress');
    const back =
      typeof jsi.stepAcrossJsiBack === 'function'
        ? jsi.stepAcrossJsiBack(fwd.nativeSymbol)
        : typeof jsi.stepToJs === 'function'
          ? jsi.stepToJs(fwd.nativeSymbol)
          : null;
    assert.ok(back, 'a back-step entry point must exist');
    assert.equal(back.jsFunction, 'onPress');
    assert.equal(back.nativeSymbol, fwd.nativeSymbol);
  });

  it('unknown C++ symbol throws a structured error, never a half mapping', () => {
    const jsi = fresh(JSI_BOUNDARY);
    assert.throws(
      () => jsi.stepAcrossJsi('__no_such_js_function__'),
      (err) =>
        err instanceof TypeError ||
        (err && typeof err.command === 'string' && typeof err.cause === 'string'),
      'unknown JS frame must throw a structured error or TypeError'
    );
  });
});

describe('devserver release-stub connect behavior pin (review MINOR)', () => {
  it("devserver connect('release') refuses with the release reason", () => {
    const { connect } = fresh(DEVSERVER_STUB);
    assert.equal(typeof connect, 'function');
    const res = connect('release');
    assert.deepEqual(Object.keys(res).sort(), ['action', 'reason', 'status']);
    assert.equal(res.status, 'refused');
    assert.equal(res.reason, 'debug channel not compiled in');
    assert.equal(typeof res.action, 'string');
    assert.ok(res.action.length > 0);
  });

  it("devserver connect('dev') pins current always-refuse behavior", () => {
    const { connect } = fresh(DEVSERVER_STUB);
    const res = connect('dev');
    // PIN (review MINOR): the devserver release artifact currently refuses
    // every buildType including 'dev'. The coder will clarify the artifact
    // vs parameter; this test pins today's behavior so any change is explicit.
    assert.equal(res.status, 'refused');
    assert.equal(res.reason, 'debug channel not compiled in');
    assert.ok(!('session' in res), 'refused result must not carry a session');
  });

  it('devserver connect with unknown/omitted buildType fails closed', () => {
    const { connect } = fresh(DEVSERVER_STUB);
    for (const bad of [undefined, null, 'prod', '', 42]) {
      const res = connect(bad);
      assert.ok(
        res && res.status === 'refused',
        `buildType ${String(bad)} must refuse, got: ${JSON.stringify(res)}`
      );
    }
  });
});
