'use strict';
// T4 — third-party env gate (NFR-BUILD-021 / AC-BUILD-078/079, TC-BUILD-079).
// Hermetic: node:test + node:assert/strict + stdlib fs/os/path/crypto/child_process
// only. Tmp fixtures under os.tmpdir(); env mutated per-test with restore;
// no network, no shared-workspace path, no secrets.
// Fixture style follows toolchain/cli/shellout-gate.test.js (mkTmp + afterEach cleanup).
// Requires T3 module toolchain/cli/thirdPartyEnv.js (do NOT reimplement it).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const childProcess = require('node:child_process');

const { resolveThirdParty, requiresThirdParty } = require('./thirdPartyEnv.js');

const VAR_NAME = 'MUSE_NATIVE_THIRD_PARTY_DIR';

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}

const savedEnv = process.env[VAR_NAME];
afterEach(() => {
  if (savedEnv === undefined) {
    delete process.env[VAR_NAME];
  } else {
    process.env[VAR_NAME] = savedEnv;
  }
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function sha256Of(filePath) {
  return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
}

// Builds: <thirdPartyDir>/<file> tarball containing "payload.txt", plus a pins
// manifest { [dep]: { file, sha256 } }. Returns { dir, pinsPath, file, sha256 }.
function makePinnedDep(dep, opts = {}) {
  const dir = opts.dir || mkTmp('tp-dir-');
  const src = mkTmp('tp-src-');
  const payload = opts.payload || `payload-for-${dep}\n`;
  fs.writeFileSync(path.join(src, 'payload.txt'), payload);
  const file = `${dep}.tar.gz`;
  const tarball = path.join(dir, file);
  childProcess.execFileSync('tar', ['-czf', tarball, '-C', src, 'payload.txt']);
  const sha256 = sha256Of(tarball);
  const pinsPath = opts.pinsPath || path.join(mkTmp('tp-pins-'), 'pins.json');
  let pins = {};
  if (fs.existsSync(pinsPath)) {
    pins = JSON.parse(fs.readFileSync(pinsPath, 'utf8'));
  }
  pins[dep] = { file, sha256 };
  fs.mkdirSync(path.dirname(pinsPath), { recursive: true });
  fs.writeFileSync(pinsPath, JSON.stringify(pins));
  return { dir, pinsPath, file, sha256 };
}

function expectVarHelp(err) {
  assert.match(String(err && err.message), new RegExp(VAR_NAME));
  assert.match(String(err && err.message), /export MUSE_NATIVE_THIRD_PARTY_DIR=/);
}

describe('T4 — happy path resolve+verify+extract', () => {
  it('(1) resolves, verifies the pin, and extracts into the tmp build tree', () => {
    const { dir, pinsPath } = makePinnedDep('dep-a');
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('tp-build-');
    const result = resolveThirdParty('dep-a', { pinsPath, buildTree });
    const expected = path.join(buildTree, 'third-party', 'dep-a');
    assert.equal(result.dir, expected);
    assert.equal(fs.readFileSync(path.join(expected, 'payload.txt'), 'utf8'), 'payload-for-dep-a\n');
    const marker = JSON.parse(fs.readFileSync(path.join(expected, '.third-party-pin.json'), 'utf8'));
    assert.equal(marker.sha256.length, 64);
  });
});

describe('T4 — fail-closed paths name the var with an example export', () => {
  it('(2) unset env throws naming the var + example export', () => {
    const { pinsPath } = makePinnedDep('dep-a');
    delete process.env[VAR_NAME];
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-a', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYENV');
      expectVarHelp(err);
      return true;
    });
  });

  it('(3) missing folder throws naming the var + example export', () => {
    const { pinsPath } = makePinnedDep('dep-a');
    const missing = path.join(mkTmp('tp-missing-'), 'no-such-dir');
    process.env[VAR_NAME] = missing;
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-a', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYDIR');
      expectVarHelp(err);
      return true;
    });
  });

  it('(4) tampered tarball (hash mismatch) throws naming the var + example export', () => {
    const { dir, pinsPath, file } = makePinnedDep('dep-a');
    fs.appendFileSync(path.join(dir, file), 'tampered-bytes');
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-a', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYHASH');
      expectVarHelp(err);
      return true;
    });
  });
});

describe('T4 — lazy gate (AC-BUILD-079)', () => {
  it('(5) env unset with no dep required succeeds', () => {
    delete process.env[VAR_NAME];
    const buildTree = mkTmp('tp-build-');
    const pinsPath = path.join(mkTmp('tp-pins-'), 'pins.json');
    fs.writeFileSync(pinsPath, '{}');
    assert.equal(requiresThirdParty(''), false);
    const result = resolveThirdParty('', { pinsPath, buildTree });
    assert.equal(result.dir, buildTree);
  });
});

describe('T4 — per-dep isolation in the build tree', () => {
  it('(6) each dep extracts to its own build-tree dir, never in-source', () => {
    const pinsDir = mkTmp('tp-pins-');
    const pinsPath = path.join(pinsDir, 'pins.json');
    const first = makePinnedDep('dep-a', { pinsPath });
    makePinnedDep('dep-b', { dir: first.dir, pinsPath, payload: 'payload-for-dep-b\n' });
    process.env[VAR_NAME] = first.dir;
    const buildTree = mkTmp('tp-build-');
    const outA = resolveThirdParty('dep-a', { pinsPath, buildTree });
    const outB = resolveThirdParty('dep-b', { pinsPath, buildTree });
    assert.notEqual(outA.dir, outB.dir);
    assert.equal(outA.dir, path.join(buildTree, 'third-party', 'dep-a'));
    assert.equal(outB.dir, path.join(buildTree, 'third-party', 'dep-b'));
    assert.equal(fs.readFileSync(path.join(outA.dir, 'payload.txt'), 'utf8'), 'payload-for-dep-a\n');
    assert.equal(fs.readFileSync(path.join(outB.dir, 'payload.txt'), 'utf8'), 'payload-for-dep-b\n');
    assert.equal(fs.existsSync(path.join(first.dir, 'payload.txt')), false);
    assert.equal(fs.existsSync(path.join(first.dir, 'third-party')), false);
  });
});

describe('T4 — pin manifest errors name the var with an example export', () => {
  it('(7) unknown dep with a valid pins file throws ETHIRDPARTYPIN', () => {
    const { dir, pinsPath } = makePinnedDep('dep-a');
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-unknown', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYPIN');
      expectVarHelp(err);
      return true;
    });
  });

  it('(8) malformed pins JSON throws ETHIRDPARTYPIN', () => {
    const { dir } = makePinnedDep('dep-a');
    process.env[VAR_NAME] = dir;
    const badPinsPath = path.join(mkTmp('tp-pins-'), 'pins.json');
    fs.writeFileSync(badPinsPath, '{ not valid json');
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-a', { pinsPath: badPinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYPIN');
      expectVarHelp(err);
      return true;
    });
  });

  it('(9) missing pins file throws ETHIRDPARTYPIN', () => {
    const { dir } = makePinnedDep('dep-a');
    process.env[VAR_NAME] = dir;
    const missingPins = path.join(mkTmp('tp-pins-'), 'no-such-pins.json');
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-a', { pinsPath: missingPins, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYPIN');
      expectVarHelp(err);
      return true;
    });
  });
});

describe('T4 — tamper-after-cache re-verifies the tarball', () => {
  it('(10) resolve green then append bytes throws ETHIRDPARTYHASH', () => {
    const { dir, pinsPath, file } = makePinnedDep('dep-a');
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('tp-build-');
    const first = resolveThirdParty('dep-a', { pinsPath, buildTree });
    assert.equal(fs.readFileSync(path.join(first.dir, 'payload.txt'), 'utf8'), 'payload-for-dep-a\n');
    fs.appendFileSync(path.join(dir, file), 'tampered-after-cache');
    assert.throws(() => resolveThirdParty('dep-a', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYHASH');
      expectVarHelp(err);
      return true;
    });
  });
});

describe('T4 — tar-slip guard rejects unsafe members', () => {
  it('(11) tarball with a ../evil member throws ETHIRDPARTYENTRY', () => {
    const dir = mkTmp('tp-dir-');
    const src = mkTmp('tp-src-');
    fs.writeFileSync(path.join(src, 'payload.txt'), 'slip-payload\n');
    const file = 'dep-evil.tar.gz';
    const tarball = path.join(dir, file);
    childProcess.execFileSync('tar', [
      '-czf', tarball, '-C', src,
      '--transform=s,payload.txt,../evil,',
      'payload.txt',
    ]);
    const sha256 = sha256Of(tarball);
    const pinsPath = path.join(mkTmp('tp-pins-'), 'pins.json');
    fs.writeFileSync(pinsPath, JSON.stringify({ 'dep-evil': { file, sha256 } }));
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('tp-build-');
    assert.throws(() => resolveThirdParty('dep-evil', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYENTRY');
      expectVarHelp(err);
      return true;
    });
  });
});
