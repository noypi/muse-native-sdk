'use strict';
// TC-PKG-004 / TC-PKG-010 / TC-PKG-019 (AC-PKG-004, AC-PKG-010, AC-PKG-019) — Batch 3 (T5 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/file lists;
// no network, no secrets, no live services, no fs fixtures.
// Binds ONLY to the pinned T5 `Interface:` lines, never to an implementation.
// Resolution: `../lib/js-only.js` once it lands, else the `../index.js` shim,
// so every behavior case below fails until T5 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let jsOnlyMod = {};
try {
  jsOnlyMod = require('../lib/js-only.js');
} catch {
  try {
    jsOnlyMod = require('../index.js');
  } catch {
    jsOnlyMod = {};
  }
}
const { isJsOnly, resolveEntry } = jsOnlyMod;

function jsOnlyManifest(overrides = {}) {
  return {
    name: 'acme-widgets',
    version: '1.0.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: [],
    engineAbi: 'deadbeef',
    exports: [],
    capabilities: [],
    ...overrides,
  };
}

const JS_ONLY_FILES = [
  'muse-package.json',
  'src/index.js',
  'src/index.d.ts',
  'integrity.json',
];

const NATIVE_FILES = [
  ...JS_ONLY_FILES,
  'native/arm64-v8a/libacme.so',
];

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
// isJsOnly — T5 contract:
// `isJsOnly(manifest: ValidatedManifest, files: string[]): boolean`
// ---------------------------------------------------------------------------
describe('TC-PKG-004 — isJsOnly (AC-PKG-004)', () => {
  it('returns true when no native/ dir is present (no error, no config needed)', () => {
    assert.equal(isJsOnly(jsOnlyManifest(), JS_ONLY_FILES), true);
  });

  it('returns false when files include a native/ library', () => {
    assert.equal(isJsOnly(jsOnlyManifest(), NATIVE_FILES), false);
  });

  it('returns true for source-form entries with no native/ dir', () => {
    const m = jsOnlyManifest({ entry: { form: 'source', main: 'src/index.tsx', types: 'src/index.d.ts' } });
    const files = ['muse-package.json', 'src/index.tsx', 'integrity.json'];
    assert.equal(isJsOnly(m, files), true);
  });

  it('returns a real boolean (not truthy junk)', () => {
    assert.equal(typeof isJsOnly(jsOnlyManifest(), JS_ONLY_FILES), 'boolean');
    assert.equal(typeof isJsOnly(jsOnlyManifest(), NATIVE_FILES), 'boolean');
  });
});

// ---------------------------------------------------------------------------
// resolveEntry — T5 contract:
// `resolveEntry(manifest: ValidatedManifest): { kind: "prebuilt" | "source",
//  main: string, types: string }`
// throws `PkgError{ code: "E_ENTRY_FORM" | "E_JS_EXPORTS",
//  path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-004 — resolveEntry (AC-PKG-004)', () => {
  it('resolves a prebuilt entry to kind/main/types', () => {
    assert.deepEqual(resolveEntry(jsOnlyManifest()), {
      kind: 'prebuilt',
      main: 'src/index.js',
      types: 'src/index.d.ts',
    });
  });

  it('resolves a source entry to kind source with main and types strings', () => {
    const m = jsOnlyManifest({ entry: { form: 'source', main: 'src/index.tsx', types: 'src/index.d.ts' } });
    const out = resolveEntry(m);
    assert.equal(out.kind, 'source');
    assert.equal(out.main, 'src/index.tsx');
    assert.equal(typeof out.types, 'string');
  });

  it('rejects an unknown entry form with E_ENTRY_FORM naming the entry', () => {
    const m = jsOnlyManifest({ entry: { form: 'bundle', main: 'src/index.js', types: 'src/index.d.ts' } });
    assertPkgError(() => resolveEntry(m), 'E_ENTRY_FORM', 'src/index.js');
  });

  it('rejects a prebuilt entry with a missing types file with E_ENTRY_FORM naming types (TC-PKG-019)', () => {
    const m = jsOnlyManifest({ entry: { form: 'prebuilt', main: 'src/index.js', types: '' } });
    assertPkgError(() => resolveEntry(m), 'E_ENTRY_FORM', 'types');
  });

  it('rejects a traversal main with E_ENTRY_FORM naming the path (security)', () => {
    const m = jsOnlyManifest({ entry: { form: 'prebuilt', main: '../evil.js', types: 'src/index.d.ts' } });
    assertPkgError(() => resolveEntry(m), 'E_ENTRY_FORM', '../evil.js');
  });
});

describe('TC-PKG-010 — JS-only manifest declaring exports is rejected (AC-PKG-010)', () => {
  it('throws E_JS_EXPORTS naming exports when a JS-only manifest declares exports', () => {
    const m = jsOnlyManifest({
      exports: [{ name: 'add', arity: 2, argKinds: ['i32', 'i32'], returnKind: 'i32' }],
    });
    assertPkgError(() => resolveEntry(m), 'E_JS_EXPORTS', 'exports');
  });
});
