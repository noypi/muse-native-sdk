'use strict';
// T1 — release/dev emission + source-map generation (NFR-BUILD-012 / AC-BUILD-036, AC-BUILD-039).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// Entry fixtures and outDirs live under os.tmpdir(); no network, no secrets,
// no live services, no hermesc binary (release bytecode is a hermetic marker
// emission per plan A3; the gate asserts the release/dev distinction, not real
// Hermes bytecode).
// Red state: ./emitBundle.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND) until T1 lands.
//
// Interface under test (plan verbatim):
//   emitBundle({ entry: string, mode: 'release'|'dev', outDir: string }): { bundlePath: string, mapPath: string, bytecode: boolean } — throws `BundleError { code, file, message }` on bad entry; `none` async surface (sync, stdlib fs only).
//
// ASSUMPTION (hermesc marker per plan A3): a release bundle is identified by a
// text marker containing "HERMES_BYTECODE" and `bytecode:true`; a dev bundle
// embeds the interpreted entry source verbatim and reports `bytecode:false`.
// ASSUMPTION (budget values perf-owned per plan A2): no size number is asserted
// here — only that both artifacts exist and are non-empty.
// ASSUMPTION (map shape): the `.map` file is JSON with string `mappings` and a
// `sources` array naming the entry (basename match); exact bundler fields
// beyond that are coder-owned.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./emitBundle.js');
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
});

const ENTRY_SRC = `'use strict';\nconsole.log('hello-muse');\n`;

function mkEntry(dir, name = 'index.js') {
  const entry = path.join(dir, name);
  fs.writeFileSync(entry, ENTRY_SRC);
  return entry;
}

function readMap(mapPath) {
  return JSON.parse(fs.readFileSync(mapPath, 'utf8'));
}

describe('T1 — emitBundle contract shape', () => {
  it('release returns { bundlePath, mapPath, bytecode:true }', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-release-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const result = emitBundle({ entry, mode: 'release', outDir });
    assert.equal(typeof result.bundlePath, 'string');
    assert.equal(typeof result.mapPath, 'string');
    assert.equal(result.bytecode, true);
  });

  it('dev returns { bundlePath, mapPath, bytecode:false }', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-dev-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const result = emitBundle({ entry, mode: 'dev', outDir });
    assert.equal(typeof result.bundlePath, 'string');
    assert.equal(typeof result.mapPath, 'string');
    assert.equal(result.bytecode, false);
  });
});

describe('T1 — release/dev emission distinction (AC-BUILD-036)', () => {
  it('release writes a Hermes-bytecode marker bundle, not interpreted source', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-marker-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const { bundlePath } = emitBundle({ entry, mode: 'release', outDir });
    const content = fs.readFileSync(bundlePath, 'utf8');
    assert.ok(content.length > 0, 'release bundle must be non-empty');
    assert.ok(
      content.includes('HERMES_BYTECODE'),
      `release bundle must carry the Hermes-bytecode marker, got: ${JSON.stringify(content.slice(0, 120))}`
    );
  });

  it('dev writes an interpreted-source bundle containing the entry source', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-interp-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const { bundlePath } = emitBundle({ entry, mode: 'dev', outDir });
    const content = fs.readFileSync(bundlePath, 'utf8');
    assert.ok(content.length > 0, 'dev bundle must be non-empty');
    assert.ok(
      content.includes('hello-muse'),
      'dev bundle must embed the interpreted entry source'
    );
    assert.ok(
      !content.includes('HERMES_BYTECODE'),
      'dev bundle must not carry the bytecode marker'
    );
  });

  it('release and dev outputs for the same entry differ', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-modes-');
    const entry = mkEntry(root);
    const rel = emitBundle({ entry, mode: 'release', outDir: path.join(root, 'out-rel') });
    const dev = emitBundle({ entry, mode: 'dev', outDir: path.join(root, 'out-dev') });
    const relContent = fs.readFileSync(rel.bundlePath, 'utf8');
    const devContent = fs.readFileSync(dev.bundlePath, 'utf8');
    assert.notEqual(relContent, devContent);
  });
});

describe('T1 — source map always emitted (AC-BUILD-039)', () => {
  for (const mode of ['release', 'dev']) {
    it(`${mode} emission produces a non-empty .map with matching sources`, () => {
      const { emitBundle } = loadImpl();
      const root = mkTmp(`emit-map-${mode}-`);
      const entry = mkEntry(root);
      const outDir = path.join(root, 'out');
      const { mapPath } = emitBundle({ entry, mode, outDir });
      assert.ok(mapPath.endsWith('.map'), `map path must end in .map, got ${mapPath}`);
      const stat = fs.statSync(mapPath);
      assert.ok(stat.size > 0, 'map file must be non-empty');
      const map = readMap(mapPath);
      assert.ok(Array.isArray(map.sources), 'map must carry a sources array');
      const named = map.sources.some((s) => String(s).includes('index.js'));
      assert.ok(
        named,
        `map sources must name the entry, got: ${JSON.stringify(map.sources)}`
      );
      assert.equal(typeof map.mappings, 'string');
      assert.ok(map.mappings.length > 0, 'map mappings must be non-empty');
    });
  }
});

describe('T1 — boundary and error cases', () => {
  it('missing entry throws BundleError { code, file, message } naming the file', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-badentry-');
    const missing = path.join(root, 'no-such-entry.js');
    assert.throws(
      () => emitBundle({ entry: missing, mode: 'release', outDir: path.join(root, 'out') }),
      (err) => {
        assert.equal(err.name, 'BundleError');
        assert.equal(typeof err.code, 'string');
        assert.ok(String(err.file || err.message).includes('no-such-entry.js'));
        assert.equal(typeof err.message, 'string');
        assert.ok(err.message.length > 0);
        return true;
      }
    );
  });

  it('directory-as-entry throws BundleError (never silently emits)', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-direntry-');
    assert.throws(
      () => emitBundle({ entry: root, mode: 'dev', outDir: path.join(root, 'out') }),
      (err) => {
        assert.equal(err.name, 'BundleError');
        assert.equal(typeof err.code, 'string');
        assert.equal(typeof err.message, 'string');
        return true;
      }
    );
  });

  it('unknown mode throws BundleError instead of emitting', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-badmode-');
    const entry = mkEntry(root);
    assert.throws(
      () => emitBundle({ entry, mode: 'staging', outDir: path.join(root, 'out') }),
      (err) => {
        assert.equal(err.name, 'BundleError');
        assert.equal(typeof err.code, 'string');
        assert.equal(typeof err.message, 'string');
        return true;
      }
    );
  });

  it('path traversal entry outside the tree throws BundleError and emits nothing', () => {
    // Input-validation denial: a malicious entry must be rejected, never read blindly.
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-traversal-');
    const outDir = path.join(root, 'out');
    assert.throws(
      () =>
        emitBundle({
          entry: path.join(root, '..', '..', 'etc', 'passwd'),
          mode: 'release',
          outDir,
        }),
      (err) => {
        assert.equal(err.name, 'BundleError');
        return true;
      }
    );
    assert.ok(!fs.existsSync(outDir) || fs.readdirSync(outDir).length === 0);
  });

  it('error messages never leak secrets from the environment', () => {
    // Fixture uses an obvious dummy value; the assertion proves it is not echoed.
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-secret-');
    process.env.MUSE_BUNDLE_TEST_DUMMY = 'test-secret-not-real-123';
    try {
      assert.throws(
        () =>
          emitBundle({
            entry: path.join(root, 'absent.js'),
            mode: 'release',
            outDir: path.join(root, 'out'),
          }),
        (err) => {
          assert.ok(!String(err.message).includes('test-secret-not-real-123'));
          return true;
        }
      );
    } finally {
      delete process.env.MUSE_BUNDLE_TEST_DUMMY;
    }
  });
});

describe('T1 — security hardening: backslash traversal (AC-BUILD-036)', () => {
  it('Windows-style ..\\..\\ traversal entry throws ETRAVERSAL and emits nothing', () => {
    // Backslash-separated `..` segments must be rejected exactly like
    // forward-slash traversal: fail-closed with ETRAVERSAL, never read blindly.
    const { emitBundle } = loadImpl();
    const root = mkTmp('emit-backslash-traversal-');
    const outDir = path.join(root, 'out');
    assert.throws(
      () =>
        emitBundle({
          entry: '..\\..\\evil.js',
          mode: 'release',
          outDir,
        }),
      (err) => {
        assert.equal(err.name, 'BundleError');
        assert.equal(err.code, 'ETRAVERSAL');
        return true;
      }
    );
    assert.ok(!fs.existsSync(outDir) || fs.readdirSync(outDir).length === 0);
  });
});
