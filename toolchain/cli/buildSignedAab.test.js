'use strict';
// T2 — Build a signed AAB (hermetic) (NFR-BUILD-005 / AC-BUILD-005 signed-AAB
// half; NFR-BUILD-015 signing/retention subset). Depends on T1 scaffolded inputs.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path/crypto) only.
// All fixtures live under os.tmpdir(); no network, no device, no secrets, no
// live services. Key material is an obvious dummy alias string, never a real key.
// Fixture syntax ASSUMPTIONs (recorded in unit-tests/T2.md): the project is a
// fake tmpdir stand-in (manifest + stub bundle inputs); the AAB is a synthetic
// artifact the coder assembles deterministically; byte-identity is asserted by
// hashing the emitted .aab bytes across two runs on identical inputs.
// Red state: toolchain/cli/buildSignedAab.js does not exist yet, so every case
// below fails until T2 lands (MODULE_NOT_FOUND = missing behavior, not a
// malformed test).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');

function loadImpl() {
  return require('./buildSignedAab.js');
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
    delete require.cache[require.resolve('./buildSignedAab.js')];
  } catch { /* not yet implemented — red state */ }
});

// Minimal scaffolded-project stand-in: manifest + stub bundle inputs the plan
// says T2 consumes (NFR-BUILD-012/013 stubs).
function writeProject(root) {
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, 'muse-native.yaml'), 'name: fake-app-not-real\n');
  fs.writeFileSync(path.join(root, 'bundle.stub.js'), "'use strict';\n");
  fs.writeFileSync(path.join(root, 'ui-tree.stub.json'), '{"tree":"fake-not-real"}\n');
  return root;
}

function sha256OfFile(p) {
  return crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
}

describe('T2 — buildSignedAab contract shape', () => {
  it('returns { ok: true, aabPath, signed: true, algorithm: rsa-pss-sha256 }', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-shape-');
    const projectDir = writeProject(path.join(root, 'app'));
    const result = buildSignedAab({ projectDir, keyAlias: 'fake-alias-not-real' });
    assert.equal(result.ok, true);
    assert.equal(typeof result.aabPath, 'string');
    assert.equal(result.signed, true);
    assert.equal(result.algorithm, 'rsa-pss-sha256');
  });

  it('emits the AAB artifact on disk at aabPath', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-emit-');
    const projectDir = writeProject(path.join(root, 'app'));
    const { aabPath } = buildSignedAab({ projectDir, keyAlias: 'fake-alias-not-real' });
    assert.ok(fs.existsSync(aabPath), `aabPath must exist on disk: ${aabPath}`);
    assert.ok(fs.statSync(aabPath).size > 0, 'emitted AAB must be non-empty');
  });

  it('is byte-identical across two runs on identical inputs (deterministic)', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-determinism-');
    const projectDir = writeProject(path.join(root, 'app'));
    const first = buildSignedAab({ projectDir, keyAlias: 'fake-alias-not-real' });
    const firstHash = sha256OfFile(first.aabPath);
    const firstBytes = fs.readFileSync(first.aabPath);
    const second = buildSignedAab({ projectDir, keyAlias: 'fake-alias-not-real' });
    const secondHash = sha256OfFile(second.aabPath);
    assert.equal(secondHash, firstHash, 'two runs on identical inputs must be byte-identical');
    assert.deepEqual(fs.readFileSync(second.aabPath), firstBytes);
  });
});

describe('T2 — signing inputs missing throw unsigned-artifact', () => {
  it('a missing keyAlias throws Error("unsigned-artifact: <reason>")', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-no-alias-');
    const projectDir = writeProject(path.join(root, 'app'));
    assert.throws(
      () => buildSignedAab({ projectDir }),
      (err) => err instanceof Error && /^unsigned-artifact: \S/.test(err.message)
    );
  });

  it('an empty keyAlias throws unsigned-artifact', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-empty-alias-');
    const projectDir = writeProject(path.join(root, 'app'));
    assert.throws(
      () => buildSignedAab({ projectDir, keyAlias: '' }),
      (err) => err instanceof Error && /^unsigned-artifact: \S/.test(err.message)
    );
  });

  it('a missing projectDir throws unsigned-artifact', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-no-project-');
    assert.throws(
      () => buildSignedAab({ projectDir: path.join(root, 'absent'), keyAlias: 'fake-alias-not-real' }),
      (err) => err instanceof Error && /^unsigned-artifact: \S/.test(err.message)
    );
  });

  it('a tampered-input fixture (manifest removed) fails with unsigned-artifact', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-tampered-');
    const projectDir = writeProject(path.join(root, 'app'));
    fs.rmSync(path.join(projectDir, 'muse-native.yaml'));
    fs.writeFileSync(path.join(projectDir, 'muse-native.yaml.tampered'), 'tampered-not-real\n');
    assert.throws(
      () => buildSignedAab({ projectDir, keyAlias: 'fake-alias-not-real' }),
      (err) => err instanceof Error && /^unsigned-artifact: \S/.test(err.message)
    );
  });

  it('never emits an unsigned AAB as success (no .aab left behind on failure)', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-no-unsigned-');
    const projectDir = writeProject(path.join(root, 'app'));
    assert.throws(() => buildSignedAab({ projectDir }));
    const leftovers = fs.readdirSync(projectDir).filter((f) => f.endsWith('.aab'));
    assert.equal(leftovers.length, 0, 'a failed signing must leave no .aab behind');
  });

  it('failure messages carry a reason and leak no key material', () => {
    const { buildSignedAab } = loadImpl();
    const root = mkTmp('aab-msg-');
    const projectDir = writeProject(path.join(root, 'app'));
    let message = '';
    try {
      buildSignedAab({ projectDir });
    } catch (err) {
      message = err.message;
    }
    assert.match(message, /^unsigned-artifact: \S.+/);
    assert.doesNotMatch(message, /fake-secret|token|BEGIN PRIVATE KEY/);
  });

  it('sentinel alias never appears in any thrown unsigned-artifact message', () => {
    const { buildSignedAab } = loadImpl();
    // Obvious dummy sentinel per header convention — never real key material.
    const SENTINEL = 'S3CR3T-ALIAS-PROBE-6F3A9C-dummy-not-real';
    const messages = [];
    function collectThrow(fn, label) {
      let message = '';
      try {
        fn();
      } catch (err) {
        message = String(err && err.message ? err.message : err);
      }
      assert.ok(message.length > 0, `${label} must throw (red if it returns)`);
      assert.match(message, /^unsigned-artifact: \S.+/, `${label} must carry unsigned-artifact reason`);
      assert.equal(
        message.includes(SENTINEL),
        false,
        `${label} must not echo the sentinel alias`
      );
      messages.push(message);
    }

    // (a) missing keyAlias — no alias supplied; sentinel must still be absent.
    {
      const root = mkTmp('aab-sentinel-a-');
      const projectDir = writeProject(path.join(root, 'app'));
      collectThrow(() => buildSignedAab({ projectDir }), 'missing keyAlias');
    }
    // (b) empty keyAlias.
    {
      const root = mkTmp('aab-sentinel-b-');
      const projectDir = writeProject(path.join(root, 'app'));
      collectThrow(() => buildSignedAab({ projectDir, keyAlias: '' }), 'empty keyAlias');
    }
    // (c1) missing projectDir string (undefined) with sentinel alias supplied.
    {
      collectThrow(() => buildSignedAab({ keyAlias: SENTINEL }), 'missing projectDir');
    }
    // (c2) project-not-found via os.tmpdir()-joined absent dir.
    {
      const root = mkTmp('aab-sentinel-c-');
      collectThrow(
        () => buildSignedAab({ projectDir: path.join(root, 'absent-not-real'), keyAlias: SENTINEL }),
        'project not found'
      );
    }
    // (c3) projectDir is not a directory (buildSignedAab.js:130).
    {
      const root = mkTmp('aab-sentinel-c3-');
      const notDir = path.join(root, 'not-a-dir-not-real');
      fs.writeFileSync(notDir, 'file-not-dir-not-real\n');
      collectThrow(
        () => buildSignedAab({ projectDir: notDir, keyAlias: SENTINEL }),
        'projectDir is not a directory'
      );
    }
    // (c4) cannot read project (buildSignedAab.js:138) via fault injection.
    {
      const root = mkTmp('aab-sentinel-c4-');
      const projectDir = writeProject(path.join(root, 'app'));
      const origReaddir = fs.readdirSync;
      const fault = Object.assign(new Error('fault-not-real'), { code: 'EIO-FAULT-NOT-REAL' });
      fs.readdirSync = () => { throw fault; };
      try {
        collectThrow(() => buildSignedAab({ projectDir, keyAlias: SENTINEL }), 'cannot read project');
      } finally {
        fs.readdirSync = origReaddir;
      }
    }
    // (d) tampered-inputs fixture (.tampered sidecar).
    {
      const root = mkTmp('aab-sentinel-d-');
      const projectDir = writeProject(path.join(root, 'app'));
      fs.writeFileSync(path.join(projectDir, 'muse-native.yaml.tampered'), 'tampered-not-real\n');
      collectThrow(() => buildSignedAab({ projectDir, keyAlias: SENTINEL }), 'tampered inputs');
    }
    // (e) missing input (remove one of the three required files).
    {
      const root = mkTmp('aab-sentinel-e-');
      const projectDir = writeProject(path.join(root, 'app'));
      fs.rmSync(path.join(projectDir, 'bundle.stub.js'));
      collectThrow(() => buildSignedAab({ projectDir, keyAlias: SENTINEL }), 'missing input');
    }
    // (e2) cannot read input (buildSignedAab.js:160) via fault injection on read.
    {
      const root = mkTmp('aab-sentinel-e2-');
      const projectDir = writeProject(path.join(root, 'app'));
      const origRead = fs.readFileSync;
      const fault = Object.assign(new Error('fault-not-real'), { code: 'EIO-FAULT-NOT-REAL' });
      fs.readFileSync = () => { throw fault; };
      try {
        collectThrow(() => buildSignedAab({ projectDir, keyAlias: SENTINEL }), 'cannot read input');
      } finally {
        fs.readFileSync = origRead;
      }
    }
    // (f) signing-failure path via fault injection on write.
    {
      const root = mkTmp('aab-sentinel-f-');
      const projectDir = writeProject(path.join(root, 'app'));
      const origWrite = fs.writeFileSync;
      const fault = Object.assign(new Error('fault-not-real'), { code: 'EIO-FAULT-NOT-REAL' });
      fs.writeFileSync = () => { throw fault; };
      try {
        collectThrow(() => buildSignedAab({ projectDir, keyAlias: SENTINEL }), 'signing failure');
      } finally {
        fs.writeFileSync = origWrite;
      }
    }

    assert.equal(messages.length, 10, 'sentinel-alias loop covers missing/empty keyAlias, missing projectDir, project-not-found, not-a-directory (:130), cannot-read-project (:138), tampered inputs, missing input, cannot-read-input (:160), signing failure');
  });
});
