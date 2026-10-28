'use strict';
// TC-PKG-001 (AC-PKG-001) — Batch 1 (T1/T2/T3 contracts, BUG-PKG-002).
// Hermetic: node:test + node:assert/strict only. Inline manifests/file lists;
// temp fixture dirs under os.tmpdir() (same convention as integrity.test.ts);
// no network, no secrets, no live services, no real .so open.
// Binds ONLY to the pinned T1/T2 `Interface:` lines, never to an implementation.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function tryRequire(path) {
  try {
    return require(path);
  } catch {
    return {};
  }
}

const manifestMod = tryRequire('../lib/manifest.js');
const layoutMod = tryRequire('../lib/layout.js');
const loaderMod = tryRequire('../lib/loader.js');
const { validateManifest } = manifestMod;
const { validateLayout } = layoutMod;
const { loadPackage } = loaderMod;

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
// validateManifest — unchanged pins (AC-PKG-001 manifest leg).
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
// throws `PkgError{ code: "E_LAYOUT", path: string, expected: string }`
// when `muse-package.json` is absent (including when only
// `capo-package.json` is present); `ROOT_FILES` no longer contains any
// `capo` name; `ALIAS_MANIFESTS` export removed.
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

  it('rejects a capo-only root with E_LAYOUT naming capo-package.json (BUG-PKG-002 fail-closed)', () => {
    const files = ['capo-package.json', 'src/index.js', 'src/index.d.ts', 'integrity.json'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', 'capo-package.json');
  });

  it('rejects a file list with neither manifest with E_LAYOUT naming muse-package.json', () => {
    const files = ['src/index.js', 'src/index.d.ts', 'integrity.json'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', 'muse-package.json');
  });

  it('ROOT_FILES no longer contains any capo name', () => {
    assert.ok(layoutMod.ROOT_FILES, 'layout must export ROOT_FILES');
    for (const entry of layoutMod.ROOT_FILES) {
      assert.ok(
        !String(entry).includes('capo'),
        `ROOT_FILES must not contain a capo name, got: ${entry}`,
      );
    }
  });

  it('ALIAS_MANIFESTS export is removed', () => {
    assert.equal(
      layoutMod.ALIAS_MANIFESTS,
      undefined,
      'layout must not export ALIAS_MANIFESTS',
    );
  });

  it('rejects a path-traversal entry naming the path (security: traversal)', () => {
    const files = [...PREBUILT_FILES, '../outside.js'];
    assertPkgError(() => validateLayout(files, validManifest()), 'E_LAYOUT', '../outside.js');
  });
});

// ---------------------------------------------------------------------------
// loadPackage — T2 contract:
// `loadPackage(pkgDir: string, opts?: object): { dir: string, manifest: object,
//  files: string[] }`
// throws `PkgError{ code: "E_LAYOUT", path: "muse-package.json",
//  expected: string }` when `muse-package.json` is absent even if
// `capo-package.json` exists; reads and parses only `muse-package.json`.
// ---------------------------------------------------------------------------
describe('TC-PKG-001 — loadPackage capo fail-closed (AC-PKG-001, loader leg)', () => {
  const fs = require('node:fs');
  const os = require('node:os');
  const nodePath = require('node:path');
  const { createHash } = require('node:crypto');

  const OPTS = { engineAbi: 'deadbeef', deviceAbi: 'arm64-v8a', grantedCapabilities: [] };

  function sha256(buf) {
    return createHash('sha256').update(buf).digest('hex');
  }

  function writeFixtureDir(manifestName, manifestOverrides = {}) {
    const manifest = validManifest(manifestOverrides);
    const dir = fs.mkdtempSync(nodePath.join(os.tmpdir(), 'pkg-layout-'));
    fs.mkdirSync(nodePath.join(dir, 'src'), { recursive: true });
    const files = {
      [manifestName]: Buffer.from(JSON.stringify(manifest)),
      'src/index.js': Buffer.from('module.exports = {};'),
      'src/index.d.ts': Buffer.from('export {};'),
    };
    const integrity = {};
    for (const [rel, buf] of Object.entries(files)) {
      fs.writeFileSync(nodePath.join(dir, rel), buf);
      integrity[rel] = sha256(buf);
    }
    fs.writeFileSync(nodePath.join(dir, 'integrity.json'), JSON.stringify(integrity));
    return dir;
  }

  function withFixtureDir(manifestName, manifestOverrides, fn) {
    const dir = writeFixtureDir(manifestName, manifestOverrides);
    try {
      fn(dir);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  }

  it('rejects a capo-only package dir with E_LAYOUT naming muse-package.json', () => {
    assert.equal(typeof loadPackage, 'function', 'loadPackage must exist');
    withFixtureDir('capo-package.json', {}, (dir) => {
      assertPkgError(() => loadPackage(dir, OPTS), 'E_LAYOUT', 'muse-package.json');
    });
  });

  it('loads a canonical muse-package.json package unchanged', () => {
    assert.equal(typeof loadPackage, 'function', 'loadPackage must exist');
    withFixtureDir('muse-package.json', {}, (dir) => {
      const out = loadPackage(dir, OPTS);
      assert.equal(out.dir, String(dir));
      assert.ok(Array.isArray(out.files), 'result must carry a files list');
      assert.ok(out.files.includes('muse-package.json'), 'files must list muse-package.json');
      assert.equal(out.manifest.name, 'acme-engine');
    });
  });
});
