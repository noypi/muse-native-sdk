'use strict';
// TC-PKG-003 (AC-PKG-003) — Batch 2 (T3 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/ABI lists;
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
const { checkAbiCoverage } = nativeMod;

function validManifest(overrides = {}) {
  return {
    name: 'acme-engine',
    version: '1.2.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: ['arm64-v8a', 'armeabi-v7a'],
    engineAbi: 'deadbeef',
    exports: [],
    capabilities: [],
    ...overrides,
  };
}

function assertPkgError(fn, expectedCode, expectedPathPart) {
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
  if (expectedPathPart !== undefined) {
    assert.ok(
      String(err.path).includes(expectedPathPart),
      `error path must name "${expectedPathPart}", got: ${err.path}`,
    );
  }
  return err;
}

// ---------------------------------------------------------------------------
// checkAbiCoverage — T3 contract:
// `checkAbiCoverage(manifest: ValidatedManifest, appAbis: string[]): void`
// throws `PkgError{ code: "E_ABI_COVERAGE", path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-003 — checkAbiCoverage (AC-PKG-003)', () => {
  it('accepts when every app ABI is covered by the manifest', () => {
    assert.doesNotThrow(() => checkAbiCoverage(validManifest(), ['arm64-v8a', 'armeabi-v7a']));
  });

  it('accepts a single covered ABI subset', () => {
    assert.doesNotThrow(() => checkAbiCoverage(validManifest(), ['arm64-v8a']));
  });

  it('refuses an app shipping an uncovered ABI naming the ABI (E_ABI_COVERAGE)', () => {
    assertPkgError(() => checkAbiCoverage(validManifest(), ['arm64-v8a', 'x86_64']), 'E_ABI_COVERAGE', 'x86_64');
  });

  it('refuses when none of the app ABIs are covered, naming a missing ABI', () => {
    assertPkgError(() => checkAbiCoverage(validManifest({ abis: ['arm64-v8a'] }), ['x86', 'x86_64']), 'E_ABI_COVERAGE', 'x86');
  });

  it('refuses when the manifest declares no ABIs', () => {
    assertPkgError(() => checkAbiCoverage(validManifest({ abis: [] }), ['arm64-v8a']), 'E_ABI_COVERAGE', 'arm64-v8a');
  });

  it('reports the covered set in expected', () => {
    const err = assertPkgError(
      () => checkAbiCoverage(validManifest(), ['x86_64']),
      'E_ABI_COVERAGE',
      'x86_64',
    );
    assert.ok(
      String(err.expected).includes('arm64-v8a'),
      `expected must list covered ABIs, got: ${err.expected}`,
    );
  });

  it('matches ABIs exactly (case-sensitive)', () => {
    assertPkgError(() => checkAbiCoverage(validManifest(), ['ARM64-V8A']), 'E_ABI_COVERAGE', 'ARM64-V8A');
  });

  it('accepts an empty app ABI list vacuously (ASSUMPTION: nothing to cover)', () => {
    assert.doesNotThrow(() => checkAbiCoverage(validManifest(), []));
  });

  it('rejects an ABI smuggling a path separator (security: traversal)', () => {
    assertPkgError(() => checkAbiCoverage(validManifest(), ['../arm64-v8a']), 'E_ABI_COVERAGE', '../arm64-v8a');
  });
});
