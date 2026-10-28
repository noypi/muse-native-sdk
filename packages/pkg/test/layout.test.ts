'use strict';
// TC-PKG-001 (AC-PKG-001) — Batch 1 (T1 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/file lists;
// no network, no secrets, no live services, no fs fixtures.
// Binds ONLY to the pinned T1 `Interface:` lines, never to an implementation.
// Resolution: `../lib/manifest.js` + `../lib/layout.js` once they land, else the
// `../index.js` shim, so every behavior case below fails until T1 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let manifestMod = {};
let layoutMod = {};
try {
  manifestMod = require('../lib/manifest.js');
} catch {
  try {
    manifestMod = require('../index.js');
  } catch {
    manifestMod = {};
  }
}
try {
  layoutMod = require('../lib/layout.js');
} catch {
  try {
    layoutMod = require('../index.js');
  } catch {
    layoutMod = {};
  }
}
const { validateManifest } = manifestMod;
const { validateLayout } = layoutMod;

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
// validateManifest — T1 contract:
// `validateManifest(manifest: object): { name: string, version: string,
//  museApi: string, entry: { form: "prebuilt" | "source", main: string,
//  types: string }, abis: string[], engineAbi: string,
//  exports: ExportEntry[], capabilities: string[] }`
// throws `PkgError{ code: "E_MANIFEST" | "E_LAYOUT" | "E_ENTRY_FORM",
//  path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-001 — validateManifest (AC-PKG-001)', () => {
  it('accepts a canonical manifest and returns the normalized fields', () => {
    const out = validateManifest(validManifest());
    assert.equal(out.name, 'acme-engine');
    assert.equal(out.version, '1.2.0');
    assert.equal(out.museApi, '1');
    assert.deepEqual(out.entry, { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' });
    assert.deepEqual(out.abis, ['arm64-v8a']);
    assert.equal(out.engineAbi, 'deadbeef');
    assert.deepEqual(out.exports, []);
    assert.deepEqual(out.capabilities, []);
  });

  it('rejects a manifest missing muse-package.json identity with E_MANIFEST naming the path', () => {
    const { name, ...noName } = validManifest();
    assertPkgError(() => validateManifest(noName), 'E_MANIFEST', 'muse-package.json');
  });

  it('rejects a manifest with no entry with E_MANIFEST naming the entry', () => {
    const m = validManifest();
    delete m.entry;
    assertPkgError(() => validateManifest(m), 'E_MANIFEST', 'entry');
  });

  it('rejects an unknown entry.form with E_MANIFEST naming the form', () => {
    const m = validManifest({ entry: { form: 'bundle', main: 'src/index.js', types: 'src/index.d.ts' } });
    assertPkgError(() => validateManifest(m), 'E_MANIFEST', 'entry.form');
  });

  it('rejects a manifest missing version with E_MANIFEST', () => {
    const { version, ...noVersion } = validManifest();
    assertPkgError(() => validateManifest(noVersion), 'E_MANIFEST', 'version');
  });
});

// ---------------------------------------------------------------------------
// validateLayout — T1 contract:
// `validateLayout(files: string[], manifest: object): void`
// throws `PkgError{ code: "E_LAYOUT" | "E_ENTRY_FORM",
//  path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-001 — validateLayout (AC-PKG-001)', () => {
  const PREBUILT_FILES = [
    'muse-package.json',
    'src/index.js',
    'src/index.d.ts',
    'integrity.json',
  ];

  it('accepts the canonical prebuilt layout', () => {
    assert.doesNotThrow(() => validateLayout(PREBUILT_FILES, validManifest()));
  });

  it('rejects a package without muse-package.json at root naming the path', () => {
    const files = ['src/index.js', 'src/index.d.ts', 'integrity.json'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', 'muse-package.json');
  });

  it('rejects files outside the declared layout naming the offending path', () => {
    const files = [...PREBUILT_FILES, 'evil/payload.sh'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', 'evil/payload.sh');
  });

  it('rejects an unknown root entry naming the path', () => {
    const files = [...PREBUILT_FILES, 'extra.txt'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', 'extra.txt');
  });

  it('accepts the deprecated capo-package.json alias manifest (ASSUMPTION per plan A3)', () => {
    const files = ['capo-package.json', 'src/index.js', 'src/index.d.ts', 'integrity.json'];
    assert.doesNotThrow(() => validateLayout(files, validManifest()));
  });

  it('rejects a path-traversal entry naming the path (security: traversal)', () => {
    const files = [...PREBUILT_FILES, '../outside.js'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', '../outside.js');
  });
});
