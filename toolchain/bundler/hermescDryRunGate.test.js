'use strict';
// T5 — hermetic marker restricted to --dry-run/test-double only
// (NFR-BUILD-020 / AC-BUILD-077). Depends on T4 (pinned hermesc).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// The hermesc collaborator (node:child_process) is stubbed in-process;
// fixtures live under os.tmpdir(). No real hermesc, no network, no secrets.
//
// Interface under test (plan verbatim):
//   emitBundle(opts: { entry: string, mode: 'release'|'dev', outDir: string, dryRun: boolean }): { bundlePath: string, mapPath: string, bytecode: boolean }
//
// ASSUMPTION (dry-run semantics): dryRun:true performs no release compile —
//   it writes NO shippable bundle/map at bundlePath/mapPath, returns
//   bytecode:false, and emits only the HERMES_BYTECODE text marker path.
// ASSUMPTION (release semantics): release dryRun:false shells the pinned
//   hermesc (T4 gate must pass first); when hermesc is unavailable the call
//   throws BundleError (never silently falls back to the marker).
// ASSUMPTION (dev semantics): dev dryRun:false keeps the existing
//   interpreted-source behavior (bytecode:false, source embedded).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const childProcess = require('node:child_process');

const EMIT_PATH = './emitBundle.js';

function loadImpl() {
  return require(EMIT_PATH);
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}

const originals = {
  execFileSync: childProcess.execFileSync,
  execSync: childProcess.execSync,
};
let patched = false;
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
  if (patched) {
    childProcess.execFileSync = originals.execFileSync;
    childProcess.execSync = originals.execSync;
    patched = false;
  }
  delete require.cache[require.resolve(EMIT_PATH)];
});

function stubHermescOk() {
  const canned = Buffer.from('hermesc version 9.9.9-test-double\n');
  childProcess.execFileSync = () => canned;
  childProcess.execSync = () => canned;
  patched = true;
}

function stubHermescMissing() {
  const boom = new Error('spawn hermesc ENOENT');
  boom.code = 'ENOENT';
  childProcess.execFileSync = () => {
    throw boom;
  };
  childProcess.execSync = () => {
    throw boom;
  };
  patched = true;
}

const ENTRY_SRC = `'use strict';\nconsole.log('hello-muse-dryrun');\n`;

function mkEntry(dir, name = 'index.js') {
  const entry = path.join(dir, name);
  fs.writeFileSync(entry, ENTRY_SRC, 'utf8');
  return entry;
}

describe('T5 — dry-run marker emits no shipped bundle (AC-BUILD-077)', () => {
  it('release dryRun:true returns bytecode:false and writes no bundle at bundlePath', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('dryrun-release-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const result = emitBundle({ entry, mode: 'release', outDir, dryRun: true });
    assert.equal(result.bytecode, false);
    assert.equal(typeof result.bundlePath, 'string');
    assert.equal(typeof result.mapPath, 'string');
    assert.equal(fs.existsSync(result.bundlePath), false);
  });

  it('release dryRun:true writes no source map at mapPath', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('dryrun-releasemap-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const result = emitBundle({ entry, mode: 'release', outDir, dryRun: true });
    assert.equal(fs.existsSync(result.mapPath), false);
  });

  it('dev dryRun:true likewise ships nothing (marker path only)', () => {
    const { emitBundle } = loadImpl();
    const root = mkTmp('dryrun-dev-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    const result = emitBundle({ entry, mode: 'dev', outDir, dryRun: true });
    assert.equal(result.bytecode, false);
    assert.equal(fs.existsSync(result.bundlePath), false);
    assert.equal(fs.existsSync(result.mapPath), false);
  });

  it('dry-run performs zero hermesc invocations (stub would throw)', () => {
    const { emitBundle } = loadImpl();
    stubHermescMissing();
    const root = mkTmp('dryrun-nohermesc-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    let result;
    assert.doesNotThrow(() => {
      result = emitBundle({ entry, mode: 'release', outDir, dryRun: true });
    });
    assert.equal(result.bytecode, false);
  });
});

describe('T5 — release path requires the pinned hermesc (AC-BUILD-076/077)', () => {
  it('release dryRun:false without hermesc throws BundleError (never the marker)', () => {
    const { emitBundle, BundleError } = loadImpl();
    stubHermescMissing();
    const root = mkTmp('release-nohermesc-');
    const entry = mkEntry(root);
    const outDir = path.join(root, 'out');
    assert.throws(
      () => emitBundle({ entry, mode: 'release', outDir, dryRun: false }),
      (err) => err instanceof BundleError
    );
  });

  it('release failure message never leaks env secrets', () => {
    const { emitBundle } = loadImpl();
    stubHermescMissing();
    process.env.MUSE_BUNDLE_TEST_DUMMY = 'top-secret-dummy-value';
    try {
      const root = mkTmp('release-secret-');
      const entry = mkEntry(root);
      const outDir = path.join(root, 'out');
      assert.throws(
        () => emitBundle({ entry, mode: 'release', outDir, dryRun: false }),
        (err) => !String(err && err.message).includes('top-secret-dummy-value')
      );
    } finally {
      delete process.env.MUSE_BUNDLE_TEST_DUMMY;
    }
  });
});

describe('T5 — input validation stays fail-closed', () => {
  it('missing entry throws BundleError', () => {
    const { emitBundle, BundleError } = loadImpl();
    const root = mkTmp('dryrun-badentry-');
    assert.throws(
      () => emitBundle({ entry: path.join(root, 'nope.js'), mode: 'release', outDir: path.join(root, 'out'), dryRun: true }),
      (err) => err instanceof BundleError
    );
  });

  it('unknown mode throws BundleError even in dry-run', () => {
    const { emitBundle, BundleError } = loadImpl();
    const root = mkTmp('dryrun-badmode-');
    const entry = mkEntry(root);
    assert.throws(
      () => emitBundle({ entry, mode: 'staging', outDir: path.join(root, 'out'), dryRun: true }),
      (err) => err instanceof BundleError
    );
  });

  it('traversal entry is refused without reading sensitive files', () => {
    const { emitBundle, BundleError } = loadImpl();
    const root = mkTmp('dryrun-traversal-');
    assert.throws(
      () => emitBundle({ entry: '../etc/passwd.js', mode: 'release', outDir: path.join(root, 'out'), dryRun: true }),
      (err) => err instanceof BundleError
    );
  });
});
