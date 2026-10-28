'use strict';
// TC-PKG-005 (AC-PKG-005) — Batch 2 (T3 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/hash strings;
// no network, no secrets, no live services, no fs fixtures.
// Binds ONLY to the pinned T3 `Interface:` line, never to an implementation.
// Resolution: `../lib/native.js` once it lands, else the `../index.js` shim,
// so every behavior case below fails until T3 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let nativeMod = {};
try {
  nativeMod = require('../lib/native.js');
} catch {
  try {
    nativeMod = require('../index.js');
  } catch {
    nativeMod = {};
  }
}
const { checkEngineAbi } = nativeMod;

function validManifest(overrides = {}) {
  return {
    name: 'acme-engine',
    version: '1.2.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: ['arm64-v8a'],
    engineAbi: 'aaaabbbb',
    exports: [],
    capabilities: [],
    ...overrides,
  };
}

function assertPkgError(fn, expectedCode) {
  let err;
  try {
    fn();
  } catch (e) {
    err = e;
  }
  assert.ok(err, 'must throw a PkgError');
  assert.equal(err.code, expectedCode, `code must be ${expectedCode}, got: ${err && err.code}`);
  assert.equal(typeof err.path, 'string', 'PkgError must carry a string path');
  assert.equal(typeof err.expected, 'string', 'PkgError must carry a string expected');
  return err;
}

// ---------------------------------------------------------------------------
// checkEngineAbi — T3 contract:
// `checkEngineAbi(manifest: ValidatedManifest, runningEngineAbi: string): void`
// throws `PkgError{ code: "E_ENGINE_ABI", path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-005 — checkEngineAbi (AC-PKG-005)', () => {
  it('accepts a matching engine ABI hash', () => {
    assert.doesNotThrow(() => checkEngineAbi(validManifest(), 'aaaabbbb'));
  });

  it('refuses a mismatched engine ABI with E_ENGINE_ABI', () => {
    assertPkgError(() => checkEngineAbi(validManifest(), 'ccccdddd'), 'E_ENGINE_ABI');
  });

  it('reports both the expected and the actual hash on mismatch', () => {
    const err = assertPkgError(() => checkEngineAbi(validManifest(), 'ccccdddd'), 'E_ENGINE_ABI');
    const reported = `${err.expected} ${err.message}`;
    assert.ok(reported.includes('aaaabbbb'), `must report expected hash, got: ${reported}`);
    assert.ok(reported.includes('ccccdddd'), `must report actual hash, got: ${reported}`);
  });

  it('names the engineAbi field in path on mismatch', () => {
    const err = assertPkgError(() => checkEngineAbi(validManifest(), 'ccccdddd'), 'E_ENGINE_ABI');
    assert.ok(
      String(err.path).includes('engineAbi'),
      `error path must name engineAbi, got: ${err.path}`,
    );
  });

  it('refuses a missing running engine ABI', () => {
    assertPkgError(() => checkEngineAbi(validManifest(), undefined), 'E_ENGINE_ABI');
  });

  it('refuses an empty running engine ABI', () => {
    assertPkgError(() => checkEngineAbi(validManifest(), ''), 'E_ENGINE_ABI');
  });

  it('refuses when the manifest declares no engine ABI (ASSUMPTION: E_ENGINE_ABI)', () => {
    const m = validManifest();
    delete m.engineAbi;
    assertPkgError(() => checkEngineAbi(m, 'aaaabbbb'), 'E_ENGINE_ABI');
  });

  it('compares hashes exactly (case-sensitive)', () => {
    assertPkgError(() => checkEngineAbi(validManifest(), 'AAAABBBB'), 'E_ENGINE_ABI');
  });

  it('does not echo more than the two hashes (security: no manifest dump)', () => {
    const err = assertPkgError(() => checkEngineAbi(validManifest(), 'ccccdddd'), 'E_ENGINE_ABI');
    const reported = `${err.expected} ${err.message}`;
    assert.ok(!reported.includes('acme-engine'), `must not leak manifest identity, got: ${reported}`);
  });
});
