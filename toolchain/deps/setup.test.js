'use strict';
// T1 — per-ABI pinned native-dep manifest resolution (fail closed)
// (FR-DEPS-009 AC-DEPS-022 / TC-DEPS-022).
// Hermetic: node:test + node:assert/strict only. No network, no secrets,
// no live services, no application config. All pins come from the unit
// under test (toolchain/deps/setup.js + pins.json); the test only passes
// ABI strings and asserts shapes/errors.
//
// Interface under test (plan verbatim):
//   resolvePins(abi: string): { version: string, sha256: string, url: string }[] | throws SetupError(code: 'EUNPINNED'|'EHASH', artifact: string, abi: string)
//
// Red state: ./setup.js does not exist yet, so every case below fails at
// require time (MODULE_NOT_FOUND) until the coder lands T1.
//
// ASSUMPTIONs (contract silent; coder must satisfy these):
//   A1. Supported ABIs are exactly arm64-v8a, armeabi-v7a, x86, x86_64
//       (egl-probe precedent named in FR-DEPS-009 Notes).
//   A2. "Unpinned" covers: unknown ABI, ABI entry missing from pins.json,
//       an artifact entry missing version/sha256/url, or a version that is
//       not an exact pin (empty, range, or tag such as latest/*).
//   A3. "Hash mismatch" (EHASH) covers a sha256 that is not 64 lowercase/
//       uppercase hex chars; checksum-vs-content verification belongs to T3.
//   A4. SetupError is an Error with string fields code/artifact/abi; for an
//       unknown ABI the artifact field is the string 'unknown'.
//   A5. resolvePins is synchronous and performs zero network calls.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { resolvePins, SetupError } = require('./setup.js');

const ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'];

function assertPinShape(entry) {
  assert.equal(typeof entry.version, 'string');
  assert.ok(entry.version.length > 0, 'version is non-empty');
  assert.equal(typeof entry.sha256, 'string');
  assert.match(entry.sha256, /^[0-9a-fA-F]{64}$/, 'sha256 is 64 hex chars');
  assert.equal(typeof entry.url, 'string');
  assert.match(entry.url, /^https:\/\//, 'url is https');
}

function assertSetupError(err, code, artifact, abi) {
  assert.ok(err instanceof Error, `is an Error, got: ${err}`);
  assert.equal(err.code, code);
  assert.equal(err.artifact, artifact);
  assert.equal(err.abi, abi);
}

describe('resolvePins — happy path (AC-DEPS-022)', () => {
  it('resolves a non-empty pin list for every supported ABI', () => {
    for (const abi of ABIS) {
      const pins = resolvePins(abi);
      assert.ok(Array.isArray(pins), `array for ${abi}`);
      assert.ok(pins.length > 0, `non-empty for ${abi}`);
      for (const entry of pins) assertPinShape(entry);
    }
  });

  it('is deterministic across calls for the same ABI', () => {
    const a = resolvePins('arm64-v8a');
    const b = resolvePins('arm64-v8a');
    assert.deepEqual(a, b);
  });

  it('per-ABI slots may differ but each stays well-formed', () => {
    const arm = resolvePins('arm64-v8a');
    const x86 = resolvePins('x86');
    assert.ok(arm.length > 0 && x86.length > 0);
    for (const entry of [...arm, ...x86]) assertPinShape(entry);
  });
});

describe('resolvePins — fail closed on unpinned (EUNPINNED)', () => {
  it('throws EUNPINNED naming artifact+ABI for an unknown ABI', () => {
    assert.throws(() => resolvePins('mips-unknown'), (err) => {
      assertSetupError(err, 'EUNPINNED', 'unknown', 'mips-unknown');
      return true;
    });
  });

  it('throws EUNPINNED for empty/missing ABI', () => {
    for (const abi of ['', null, undefined]) {
      assert.throws(() => resolvePins(abi), (err) => {
        assert.equal(err.code, 'EUNPINNED');
        return true;
      }, `abi ${String(abi)} must fail closed`);
    }
  });

  it('SetupError carries code/artifact/abi fields', () => {
    try {
      resolvePins('definitely-not-an-abi');
      assert.fail('must throw');
    } catch (err) {
      assert.equal(err.code, 'EUNPINNED');
      assert.equal(typeof err.artifact, 'string');
      assert.equal(typeof err.abi, 'string');
    }
  });
});

describe('resolvePins — fail closed on hash problems (EHASH)', () => {
  it('every recorded pin carries a well-formed sha256 (A3)', () => {
    for (const abi of ABIS) {
      for (const entry of resolvePins(abi)) {
        assert.match(entry.sha256, /^[0-9a-fA-F]{64}$/, `malformed sha must fail closed, got: ${entry.sha256}`);
      }
    }
  });

  it('SetupError export (when present) builds EUNPINNED/EHASH errors', () => {
    if (typeof SetupError !== 'function') return; // ASSUMPTION-optional export
    const err = new SetupError('EUNPINNED', 'some-artifact', 'arm64-v8a');
    assert.ok(err instanceof Error);
    assert.equal(err.code, 'EUNPINNED');
    assert.equal(err.artifact, 'some-artifact');
    assert.equal(err.abi, 'arm64-v8a');
  });
});

describe('resolvePins — security coverage', () => {
  it('names a hostile ABI without throwing the wrong error or leaking secrets', () => {
    const hostile = '../../etc/passwd\nDROP TABLE x';
    assert.throws(() => resolvePins(hostile), (err) => {
      assert.equal(err.code, 'EUNPINNED');
      assert.ok(String(err.abi).includes(hostile));
      assert.ok(!/token|secret|password|key/i.test(String(err.message)), `no secret material in: ${err.message}`);
      return true;
    });
  });

  it('performs zero network calls (sync pure manifest read, A5)', () => {
    const before = typeof globalThis.fetch === 'function' ? globalThis.fetch : undefined;
    let calls = 0;
    globalThis.fetch = (...args) => { calls += 1; return before ? before(...args) : Promise.reject(new Error('no network')); };
    try {
      resolvePins('arm64-v8a');
    } finally {
      if (before === undefined) delete globalThis.fetch;
      else globalThis.fetch = before;
    }
    assert.equal(calls, 0);
  });
});

describe('resolvePins — Cronet AAR pins per ABI (T6 / FR-DEPS-010 AC-DEPS-026)', () => {
  // Authoritative pins transcribed from FR-DEPS-010 Notes (specs-cpp-dependencies.md:307);
  // the API-coord sha256 below is the spec/test value `d0cbb75a…faddcf8`
  // (dispatch briefs transcribed it with a typo — spec wins).
  const CRONET = [
    {
      match: 'cronet-bundled',
      version: '500.1.0',
      sha256: 'fa286aeebe66dcab4a64df017d566cb8ec563c3bb63af527997c3a4e056bf295',
      url: 'https://maven.google.com/org/chromium/net/cronet-bundled/500.1.0/cronet-bundled-500.1.0.aar',
    },
    {
      match: 'cronet',
      version: '500.1.0',
      sha256: 'd0cbb75a3e558166a1e21747d75b788b2650ddf8bec321b727b0878f01addcf8',
      url: 'https://maven.google.com/org/chromium/net/cronet/500.1.0/cronet-500.1.0.aar',
    },
  ];

  function artifactOf(entry) {
    return String(entry.artifact ?? entry.name ?? '');
  }

  function findCronet(pins, want) {
    const cands = pins.filter((e) => artifactOf(e).includes(want.match));
    if (want.match === 'cronet') {
      // Exclude the bundled sibling: the API artifact is the bare `cronet` coord.
      return cands.find((e) => !artifactOf(e).includes('cronet-bundled'));
    }
    return cands[0];
  }

  for (const abi of ABIS) {
    it(`includes both Cronet AAR pins for ${abi} with verbatim version/sha256/url`, () => {
      const pins = resolvePins(abi);
      for (const want of CRONET) {
        const got = findCronet(pins, want);
        assert.ok(got, `${want.match}:500.1.0 pinned for ${abi}`);
        assert.equal(got.version, want.version);
        assert.equal(String(got.sha256).toLowerCase(), want.sha256);
        assert.equal(got.url, want.url);
      }
    });
  }

  it('Cronet pins carry well-formed sha256 + https AAR urls on every ABI', () => {
    for (const abi of ABIS) {
      const pins = resolvePins(abi);
      for (const want of CRONET) {
        const got = findCronet(pins, want);
        assert.ok(got, `${want.match} present for ${abi}`);
        assert.match(String(got.sha256), /^[0-9a-fA-F]{64}$/);
        assert.match(String(got.url), /^https:\/\//);
        assert.ok(String(got.url).endsWith('.aar'), `AAR url, got: ${got.url}`);
      }
    }
  });

  it('SetupError contract is unchanged (EUNPINNED|EHASH with artifact+abi fields)', () => {
    assert.throws(() => resolvePins('mips-unknown'), (err) => {
      assert.ok(['EUNPINNED', 'EHASH'].includes(err.code));
      assert.equal(typeof err.artifact, 'string');
      assert.equal(typeof err.abi, 'string');
      return true;
    });
  });
});
