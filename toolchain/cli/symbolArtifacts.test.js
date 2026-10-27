'use strict';
// T4 — Unstripped symbols (NFR-BUILD-005 / AC-BUILD-005 symbolication half;
// NFR-BUILD-016 retention subset). Depends on T2.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only. All
// fixtures live under os.tmpdir(); no network, no device, no secrets, no live
// services. Symbol artifacts are stub files with obvious dummy content.
// Fixture syntax ASSUMPTIONs (recorded in unit-tests/T4.md): the project is a
// fake tmpdir stand-in; unstripped .so files are stub binaries; the strip map
// is a stub text file; the mapping ref is an opaque string the coder returns.
// Red state: toolchain/cli/symbolArtifacts.js does not exist yet, so every
// case below fails until T4 lands (MODULE_NOT_FOUND = missing behavior, not a
// malformed test).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./symbolArtifacts.js');
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
  try {
    delete require.cache[require.resolve('./symbolArtifacts.js')];
  } catch { /* not yet implemented — red state */ }
});

// Project stand-in carrying unstripped .so stubs + strip map + mapping ref.
function writeProjectWithSymbols(root) {
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, 'muse-native.yaml'), 'name: fake-app-not-real\n');
  const libDir = path.join(root, 'lib', 'arm64-v8a');
  fs.mkdirSync(libDir, { recursive: true });
  fs.writeFileSync(path.join(libDir, 'libengine-unstripped.so'), 'unstripped-stub-not-real\n');
  fs.writeFileSync(path.join(libDir, 'libui-unstripped.so'), 'unstripped-stub-not-real\n');
  fs.writeFileSync(path.join(root, 'strip-map.txt'), 'libengine-unstripped.so -> libengine.so\n');
  fs.writeFileSync(path.join(root, 'mapping.ref'), 'mapping-fake-not-real\n');
  return root;
}

describe('T4 — collectSymbols contract shape', () => {
  it('returns { ok: true, unstrippedSo, stripMapPath, mappingRef }', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-shape-');
    const projectDir = writeProjectWithSymbols(path.join(root, 'app'));
    const result = collectSymbols({ projectDir });
    assert.equal(result.ok, true);
    assert.ok(Array.isArray(result.unstrippedSo), 'unstrippedSo must be an array');
    assert.ok(result.unstrippedSo.length > 0, 'unstrippedSo must be non-empty');
    assert.equal(typeof result.stripMapPath, 'string');
    assert.equal(typeof result.mappingRef, 'string');
  });

  it('every reported unstripped .so exists on disk and the strip map exists', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-exist-');
    const projectDir = writeProjectWithSymbols(path.join(root, 'app'));
    const { unstrippedSo, stripMapPath } = collectSymbols({ projectDir });
    for (const so of unstrippedSo) {
      assert.ok(fs.existsSync(so), `unstripped .so must exist on disk: ${so}`);
    }
    assert.ok(fs.existsSync(stripMapPath), `strip map must exist on disk: ${stripMapPath}`);
  });
});

describe('T4 — missing artifacts throw symbols-missing', () => {
  it('a missing projectDir throws Error("symbols-missing: <reason>")', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-no-project-');
    assert.throws(
      () => collectSymbols({ projectDir: path.join(root, 'absent') }),
      (err) => err instanceof Error && /^symbols-missing: \S/.test(err.message)
    );
  });

  it('a project with no symbol artifacts at all throws symbols-missing', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-empty-');
    const projectDir = path.join(root, 'app');
    fs.mkdirSync(projectDir, { recursive: true });
    fs.writeFileSync(path.join(projectDir, 'muse-native.yaml'), 'name: fake-app-not-real\n');
    assert.throws(
      () => collectSymbols({ projectDir }),
      (err) => err instanceof Error && /^symbols-missing: \S/.test(err.message)
    );
  });

  it('a stripped-only set (no unstripped .so) is never reported ok', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-stripped-only-');
    const projectDir = path.join(root, 'app');
    fs.mkdirSync(root, { recursive: true });
    fs.mkdirSync(path.join(projectDir, 'lib', 'arm64-v8a'), { recursive: true });
    fs.mkdirSync(projectDir, { recursive: true });
    fs.writeFileSync(path.join(projectDir, 'lib', 'arm64-v8a', 'libengine.so'), 'stripped-stub-not-real\n');
    fs.writeFileSync(path.join(projectDir, 'strip-map.txt'), 'libengine-unstripped.so -> libengine.so\n');
    assert.throws(
      () => collectSymbols({ projectDir }),
      (err) => err instanceof Error && /^symbols-missing: \S/.test(err.message)
    );
  });

  it('unstripped .so files without a strip map throw symbols-missing', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-no-map-');
    const projectDir = writeProjectWithSymbols(path.join(root, 'app'));
    fs.rmSync(path.join(projectDir, 'strip-map.txt'));
    assert.throws(
      () => collectSymbols({ projectDir }),
      (err) => err instanceof Error && /^symbols-missing: \S/.test(err.message)
    );
  });

  it('non-string projectDir is rejected with symbols-missing (input validation)', () => {
    const { collectSymbols } = loadImpl();
    assert.throws(
      () => collectSymbols({ projectDir: 42 }),
      (err) => err instanceof Error && /^symbols-missing: \S/.test(err.message)
    );
    assert.throws(
      () => collectSymbols({}),
      (err) => err instanceof Error && /^symbols-missing: \S/.test(err.message)
    );
  });

  it('failure messages carry a reason and leak no fixture internals', () => {
    const { collectSymbols } = loadImpl();
    const root = mkTmp('sym-msg-');
    let message = '';
    try {
      collectSymbols({ projectDir: path.join(root, 'absent') });
    } catch (err) {
      message = err.message;
    }
    assert.match(message, /^symbols-missing: \S.+/);
    assert.doesNotMatch(message, /fake-secret|token|BEGIN PRIVATE KEY/);
  });
});
