'use strict';
// TC-PKG-018 + TC-PKG-019 entry-form half (AC-PKG-018, AC-PKG-019 entry half)
// — Batch 1 (T1 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/file lists;
// no network, no secrets, no live services, no fs fixtures.
// Binds ONLY to the pinned T1 `Interface:` lines, never to an implementation.
// Resolution: `../lib/entry-form.js` (+ manifest/layout) once they land, else
// the `../index.js` shim, so every behavior case below fails until T1 lands.

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
  layoutMod = require('../lib/entry-form.js');
} catch {
  try {
    layoutMod = require('../lib/layout.js');
  } catch {
    try {
      layoutMod = require('../index.js');
    } catch {
      layoutMod = {};
    }
  }
}
const { validateManifest } = manifestMod;
const { validateLayout } = layoutMod;

function prebuiltManifest(overrides = {}) {
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

function sourceManifest(overrides = {}) {
  return prebuiltManifest({
    entry: { form: 'source', main: 'src/index.tsx', types: '' },
    ...overrides,
  });
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
      String(err.path).includes(expectedPathPart) || String(err.expected).includes(expectedPathPart),
      `error must name "${expectedPathPart}" in path or expected, got path=${err.path} expected=${err.expected}`,
    );
  }
  return err;
}

// ---------------------------------------------------------------------------
// AC-PKG-018 — manifest entry.form declares prebuilt|source; src/ must match.
// ---------------------------------------------------------------------------
describe('TC-PKG-018 — entry form match (AC-PKG-018)', () => {
  it('accepts prebuilt form with src/index.js plus src/index.d.ts', () => {
    const m = validateManifest(prebuiltManifest());
    assert.doesNotThrow(() =>
      validateLayout(
        ['muse-package.json', 'src/index.js', 'src/index.d.ts', 'integrity.json'],
        m,
      ),
    );
  });

  it('rejects prebuilt form whose src/ lacks index.js naming the expected entry', () => {
    const m = validateManifest(prebuiltManifest());
    assertPkgError(
      () => validateLayout(['muse-package.json', 'src/index.d.ts', 'integrity.json'], m),
      'E_ENTRY_FORM',
      'src/index.js',
    );
  });

  it('rejects prebuilt form whose src/ lacks index.d.ts naming the expected entry', () => {
    const m = validateManifest(prebuiltManifest());
    assertPkgError(
      () => validateLayout(['muse-package.json', 'src/index.js', 'integrity.json'], m),
      'E_ENTRY_FORM',
      'src/index.d.ts',
    );
  });

  it('accepts source form with src/index.tsx', () => {
    const m = validateManifest(sourceManifest());
    assert.doesNotThrow(() =>
      validateLayout(['muse-package.json', 'src/index.tsx', 'integrity.json'], m),
    );
  });

  it('rejects source form whose src/ holds only the prebuilt entry naming the expected entry (ASSUMPTION: .ts/.tsx accepted per plan)', () => {
    const m = validateManifest(sourceManifest());
    assertPkgError(
      () => validateLayout(['muse-package.json', 'src/index.js', 'src/index.d.ts', 'integrity.json'], m),
      'E_ENTRY_FORM',
      'src/index.tsx',
    );
  });
});

// ---------------------------------------------------------------------------
// AC-PKG-019 entry half — prebuilt without matching index.d.ts is rejected.
// ---------------------------------------------------------------------------
describe('TC-PKG-019 — prebuilt d.ts rule, entry half (AC-PKG-019)', () => {
  it('rejects a prebuilt entry whose types file is absent from src/ naming index.d.ts', () => {
    const m = validateManifest(prebuiltManifest());
    const err = assertPkgError(
      () => validateLayout(['muse-package.json', 'src/index.js', 'integrity.json'], m),
      'E_ENTRY_FORM',
      'src/index.d.ts',
    );
    assert.ok(
      String(err.expected).includes('src/index.d.ts'),
      `expected must name the missing d.ts, got: ${err.expected}`,
    );
  });

  it('rejects an unknown entry.form at manifest validation with E_MANIFEST', () => {
    assertPkgError(
      () => validateManifest(prebuiltManifest({ entry: { form: 'wasm', main: 'src/index.js', types: '' } })),
      'E_MANIFEST',
      'entry.form',
    );
  });

  it('error for entry mismatch names the expected entry, not just the form (exactness)', () => {
    const m = validateManifest(prebuiltManifest());
    let err;
    try {
      validateLayout(['muse-package.json', 'src/index.tsx', 'integrity.json'], m);
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'must throw');
    assert.equal(err.code, 'E_ENTRY_FORM');
    assert.ok(
      String(err.expected).includes('src/index.js'),
      `expected must name src/index.js, got: ${err.expected}`,
    );
  });
});
