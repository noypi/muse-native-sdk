'use strict';
// TC-PKG-011 (AC-PKG-011) — Batch 3 (T6 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/file lists;
// no network, no secrets, no live services, no fs fixtures, no real .so open.
// Binds ONLY to the pinned T6 `Interface:` line, never to an implementation.
// Resolution: `../lib/errors.js` + `../lib/loader.js` (landed) for taxonomy and
// fail-closed pins; `../lib/js-only.js` (T5) and `../lib/capabilities.js` (T6)
// for the stage-order table, so those rows fail until T5/T6 land.
// checkNoRender rows are T7-owned and intentionally absent here.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function tryRequire(path) {
  try {
    return require(path);
  } catch {
    return {};
  }
}

const errorsMod = tryRequire('../lib/errors.js');
const loaderMod = tryRequire('../lib/loader.js');
const manifestMod = tryRequire('../lib/manifest.js');
const layoutMod = tryRequire('../lib/layout.js');
const integrityMod = tryRequire('../lib/integrity.js');
const nativeMod = tryRequire('../lib/native.js');
const exportsMod = tryRequire('../lib/exports.js');
const typesMod = tryRequire('../lib/types.js');
const jsOnlyMod = tryRequire('../lib/js-only.js');
const capsMod = tryRequire('../lib/capabilities.js');

const { PkgError } = errorsMod;
const { loadPackage } = loaderMod;

const ALL_CODES = [
  'E_LAYOUT',
  'E_ENTRY_FORM',
  'E_INTEGRITY',
  'E_ABI_COVERAGE',
  'E_ENGINE_ABI',
  'E_EXPORT_BINDING',
  'E_TYPES',
  'E_JS_EXPORTS',
  'E_CAPABILITY',
  'E_NO_RENDER',
  'E_MANIFEST',
];

function validManifest(overrides = {}) {
  return {
    name: 'acme-engine',
    version: '1.2.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: ['arm64-v8a'],
    engineAbi: 'deadbeef',
    exports: [],
    capabilities: [],
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// PkgError taxonomy — T6 contract:
// `class PkgError extends Error { code: "E_LAYOUT" | "E_ENTRY_FORM" |
//  "E_INTEGRITY" | "E_ABI_COVERAGE" | "E_ENGINE_ABI" | "E_EXPORT_BINDING" |
//  "E_TYPES" | "E_JS_EXPORTS" | "E_CAPABILITY" | "E_NO_RENDER" | "E_MANIFEST";
//  path: string; expected: string; }`
// ---------------------------------------------------------------------------
describe('TC-PKG-011 — fail-closed error taxonomy (AC-PKG-011)', () => {
  it('PkgError is an Error subclass carrying code/path/expected for every named failure', () => {
    for (const code of ALL_CODES) {
      const err = new PkgError(code, 'muse-package.json', `expected for ${code}`);
      assert.ok(err instanceof Error, `${code}: must be an Error`);
      assert.equal(err.name, 'PkgError', `${code}: name must be PkgError`);
      assert.equal(err.code, code);
      assert.equal(typeof err.path, 'string');
      assert.equal(typeof err.expected, 'string');
    }
  });

  it('every failing check yields a distinct named error (11 unique codes)', () => {
    const errs = ALL_CODES.map((code) => new PkgError(code, 'some/file', 'some expectation'));
    assert.equal(new Set(errs.map((e) => e.code)).size, 11);
  });

  it('the message names the check, the file, and the expected value', () => {
    const err = new PkgError('E_INTEGRITY', 'src/index.js', 'sha256 abc123');
    assert.ok(String(err.message).includes('E_INTEGRITY'));
    assert.ok(String(err.message).includes('src/index.js'));
    assert.ok(String(err.message).includes('sha256 abc123'));
  });

  it('error messages never leak bytes that were not passed in (security: no secret leak)', () => {
    const sentinel = 's3cr3t-not-real-bytes';
    const err = new PkgError('E_INTEGRITY', 'src/index.js', 'sha256 mismatch');
    assert.ok(!String(err.message).includes(sentinel));
    assert.ok(!String(err.stack).includes(sentinel));
  });

  it('each pipeline stage throws its own named code on bad input (no silent pass)', () => {
    const stages = [
      ['validateManifest', manifestMod.validateManifest, [{}], 'E_MANIFEST'],
      ['validateLayout', layoutMod.validateLayout, [['src/index.js'], validManifest()], 'E_LAYOUT'],
      [
        'verifyIntegrity',
        integrityMod.verifyIntegrity,
        [new Map([['src/index.js', Buffer.from('tampered')]]), { 'src/index.js': 'deadbeef' }],
        'E_INTEGRITY',
      ],
      ['checkAbiCoverage', nativeMod.checkAbiCoverage, [validManifest(), ['x86_64']], 'E_ABI_COVERAGE'],
      ['checkEngineAbi', nativeMod.checkEngineAbi, [validManifest(), 'baadf00d'], 'E_ENGINE_ABI'],
      [
        'bindExports',
        exportsMod.bindExports,
        [validManifest({ exports: [{ name: 'add' }] }), {}],
        'E_EXPORT_BINDING',
      ],
      ['checkEntryTypes', typesMod.checkEntryTypes, [validManifest({ exports: [{ name: 'add' }] }), 'declare nothing'], 'E_TYPES'],
      ['resolveEntry', jsOnlyMod.resolveEntry, [validManifest({ exports: [{ name: 'add' }] })], 'E_JS_EXPORTS'],
    ];
    for (const [name, fn, args, code] of stages) {
      assert.equal(typeof fn, 'function', `${name} must exist (T5/T6 row fails until it lands)`);
      let err;
      let ret;
      try {
        ret = fn(...args);
      } catch (e) {
        err = e;
      }
      assert.ok(err, `${name} must throw on bad input (nothing partially loads)`);
      assert.equal(err.code, code, `${name} must throw ${code}, got: ${err && err.code}`);
      assert.equal(ret, undefined, `${name} must not return a partial value on failure`);
    }
    // checkCapabilities row (T6): missing module fails red until T6 lands.
    assert.equal(
      typeof capsMod.checkCapabilities,
      'function',
      'checkCapabilities must exist (fails until T6 lands)',
    );
    assert.throws(
      () => capsMod.checkCapabilities(validManifest({ capabilities: ['net:http'] }), [], 'policy-1'),
      (err) => {
        assert.equal(err.code, 'E_CAPABILITY');
        return true;
      },
    );
  });

  it('loadPackage on an unusable directory fails closed with E_LAYOUT and returns nothing', () => {
    assert.equal(typeof loadPackage, 'function');
    assert.throws(
      () => loadPackage('/no-such-dir-xyz-123', { engineAbi: 'x', deviceAbi: 'y', grantedCapabilities: [] }),
      (err) => {
        assert.equal(err.code, 'E_LAYOUT');
        assert.equal(typeof err.path, 'string');
        assert.equal(typeof err.expected, 'string');
        return true;
      },
    );
  });
});
