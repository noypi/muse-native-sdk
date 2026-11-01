'use strict';
// T2 — content-keyed local mirror with zero-network offline re-run
// (FR-DEPS-009 AC-DEPS-023 / TC-DEPS-023).
// Hermetic: node:test + node:assert/strict only. No real network —
// globalThis.fetch is replaced with a counting stub in every test that
// could touch the network, and restored in teardown. No secrets, no live
// services, no application config. Mirror files live under the
// implementation's own mirror root; the test seeds warmth only through
// the public fetchToMirror with stubbed fetch, never with real downloads.
//
// Interface under test (plan verbatim):
//   fetchToMirror(artifact: string, abi: string, opts: { offline: boolean }): { path: string, hit: boolean } | throws SetupError(code: 'EOFFLINE', artifact: string, abi: string)
//
// Red state: ./mirror.js (or ./setup.js) does not exist yet, so every case
// below fails at require time (MODULE_NOT_FOUND) until the coder lands T2.
//
// ASSUMPTIONs (contract silent; coder must satisfy these):
//   A1. All network traffic goes through globalThis.fetch, so the stub
//       observes every call; a warm-mirror offline re-run makes zero calls.
//   A2. Mirror path is content-keyed: the returned path embeds the pin's
//       sha256 (64 hex chars) for that artifact+ABI.
//   A3. Return shape: { path: string (non-empty), hit: boolean }; hit is
//       false on first fetch, true when the bytes were already mirrored.
//   A4. opts defaults to online ({ offline: false }); offline:true with a
//       cold mirror throws EOFFLINE before any network attempt.
//   A5. A repeat online fetch of the same artifact+ABI is a hit and makes
//       no additional network call (idempotent reuse).

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');

let fetchToMirror;
try {
  ({ fetchToMirror } = require('./mirror.js'));
} catch {
  ({ fetchToMirror } = require('./setup.js'));
}

const ARTIFACT = 'e2e-fixture-native-lib';
const ABI = 'arm64-v8a';

let realFetch;
let calls;

function stubFetch(payload = Buffer.from('fake-bytes')) {
  calls = 0;
  realFetch = globalThis.fetch;
  globalThis.fetch = async () => {
    calls += 1;
    return new Response(payload, { status: 200 });
  };
}

function restoreFetch() {
  if (realFetch === undefined) delete globalThis.fetch;
  else globalThis.fetch = realFetch;
}

beforeEach(() => stubFetch());
afterEach(() => restoreFetch());

function assertSetupError(err, code, artifact, abi) {
  assert.ok(err instanceof Error, `is an Error, got: ${err}`);
  assert.equal(err.code, code);
  assert.equal(err.artifact, artifact);
  assert.equal(err.abi, abi);
}

describe('fetchToMirror — interface and content-keyed path (AC-DEPS-023)', () => {
  it('exports a function fetchToMirror', () => {
    assert.equal(typeof fetchToMirror, 'function');
  });

  it('online fetch returns a non-empty path that embeds the sha256 (A2/A3)', async () => {
    const out = await fetchToMirror(ARTIFACT, ABI, { offline: false });
    assert.equal(typeof out.path, 'string');
    assert.ok(out.path.length > 0);
    assert.match(out.path, /[0-9a-fA-F]{64}/, `path is content-keyed, got: ${out.path}`);
    assert.equal(typeof out.hit, 'boolean');
  });

  it('repeat fetch of the same artifact+ABI is a hit with no extra network (A5)', async () => {
    const first = await fetchToMirror(ARTIFACT, ABI, { offline: false });
    const callsAfterFirst = calls;
    const second = await fetchToMirror(ARTIFACT, ABI, { offline: false });
    assert.equal(second.path, first.path);
    assert.equal(second.hit, true);
    assert.equal(calls, callsAfterFirst, 'warm reuse makes no network call');
  });
});

describe('fetchToMirror — offline fail closed (EOFFLINE)', () => {
  it('throws EOFFLINE naming artifact+ABI when the mirror is cold', async () => {
    const artifact = `cold-artifact-${Date.now()}-a`;
    await assert.rejects(() => fetchToMirror(artifact, ABI, { offline: true }), (err) => {
      assertSetupError(err, 'EOFFLINE', artifact, ABI);
      return true;
    });
  });

  it('cold offline attempt makes zero network calls (A4)', async () => {
    const artifact = `cold-artifact-${Date.now()}-b`;
    await assert.rejects(() => fetchToMirror(artifact, ABI, { offline: true }));
    assert.equal(calls, 0, 'offline cold must not touch the network');
  });

  it('offline defaults to online when opts is omitted (A4)', async () => {
    const out = await fetchToMirror(ARTIFACT, ABI);
    assert.equal(typeof out.path, 'string');
    assert.ok(out.path.length > 0);
  });
});

describe('fetchToMirror — warm offline re-run is zero-network', () => {
  it('offline re-run with a warm mirror returns the same path with hit:true and zero fetch calls', async () => {
    const artifact = `warm-artifact-${Date.now()}`;
    const online = await fetchToMirror(artifact, ABI, { offline: false });
    stubFetch(); // reset counter; mirror stays warm
    const offline = await fetchToMirror(artifact, ABI, { offline: true });
    assert.equal(offline.path, online.path);
    assert.equal(offline.hit, true);
    assert.equal(calls, 0, 'warm offline re-run performs zero network calls');
  });
});

describe('fetchToMirror — security coverage', () => {
  it('names a hostile artifact without leaking secrets', async () => {
    const hostile = '../../etc/passwd\nDROP TABLE x';
    await assert.rejects(() => fetchToMirror(hostile, ABI, { offline: true }), (err) => {
      assert.equal(err.code, 'EOFFLINE');
      assert.ok(!/token|secret|password/i.test(String(err.message)), `no secret material in: ${err.message}`);
      return true;
    });
    assert.equal(calls, 0);
  });

  it('rejects an unknown ABI fail-closed without network', async () => {
    await assert.rejects(() => fetchToMirror(ARTIFACT, 'mips-unknown', { offline: true }), (err) => {
      assert.ok(err instanceof Error);
      assert.ok(!/token|secret|password/i.test(String(err.message)));
      return true;
    });
    assert.equal(calls, 0);
  });
});
