'use strict';
// Batch 2 (T3) — D3 resolver unit tests for `vendor-d3`.
// Hermetic: node:test + node:assert/strict + stdlib fs/os/path/crypto/
// child_process only. Tmp fixture third-party dirs under os.tmpdir() with
// self-computed pins (never the real staged tarballs, never the network);
// env mutated per-test with restore.
// Fixture style follows toolchain/cli/thirdPartyEnvGate.test.js (mkTmp +
// afterEach cleanup). Requires toolchain/cli/thirdPartyEnv.js (do NOT
// reimplement it). Per GR-08 T1.8 this file is assigned to T3 and mapped
// in unit-tests/T3.md (T4 maps the structural file only).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const childProcess = require('node:child_process');

const { resolveThirdParty, requiresThirdParty } = require('./thirdPartyEnv.js');

const VAR_NAME = 'MUSE_NATIVE_THIRD_PARTY_DIR';

// D3 dep key names per tools/deps/third_party_pins.json (Batch 1 T1).
const D3_DEPS = ['harfbuzz', 'icu4c', 'freetype'];

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

// Builds a fixture third-party dir holding one tiny tarball per D3 dep,
// plus a pins manifest { [dep]: { file, sha256 } } with self-computed
// hashes. Returns { dir, pinsPath }.
function makeD3Fixture(deps = D3_DEPS) {
  const dir = mkTmp('d3-dir-');
  const pinsPath = path.join(mkTmp('d3-pins-'), 'pins.json');
  const pins = {};
  for (const dep of deps) {
    const src = mkTmp('d3-src-');
    fs.writeFileSync(path.join(src, 'payload.txt'), `payload-for-${dep}\n`);
    const file = `${dep}.tar.gz`;
    childProcess.execFileSync('tar', ['-czf', path.join(dir, file), '-C', src, 'payload.txt']);
    pins[dep] = { file, sha256: sha256Of(path.join(dir, file)) };
  }
  fs.writeFileSync(pinsPath, JSON.stringify(pins));
  return { dir, pinsPath };
}

function expectVarHelp(err) {
  assert.match(String(err && err.message), new RegExp(VAR_NAME));
  assert.match(String(err && err.message), /export MUSE_NATIVE_THIRD_PARTY_DIR=/);
}

describe('T3 — D3 deps resolve from a fixture third-party dir', () => {
  it('(D3-1) each D3 dep resolves, verifies, and extracts per-dep', () => {
    const { dir, pinsPath } = makeD3Fixture();
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('d3-build-');
    for (const dep of D3_DEPS) {
      const result = resolveThirdParty(dep, { pinsPath, buildTree });
      const expected = path.join(buildTree, 'third-party', dep);
      assert.equal(result.dir, expected);
      assert.equal(
        fs.readFileSync(path.join(expected, 'payload.txt'), 'utf8'),
        `payload-for-${dep}\n`,
      );
    }
  });

  it('(D3-2) D3 dep dirs are isolated per-dep, never in-source', () => {
    const { dir, pinsPath } = makeD3Fixture();
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('d3-build-');
    const outA = resolveThirdParty('harfbuzz', { pinsPath, buildTree });
    const outB = resolveThirdParty('freetype', { pinsPath, buildTree });
    assert.notEqual(outA.dir, outB.dir);
    assert.equal(fs.existsSync(path.join(dir, 'third-party')), false);
  });
});

describe('T3 — D3 fail-closed paths name the var with an example export', () => {
  it('(D3-3) unset env throws ETHIRDPARTYENV for a D3 dep', () => {
    const { pinsPath } = makeD3Fixture();
    delete process.env[VAR_NAME];
    const buildTree = mkTmp('d3-build-');
    assert.throws(() => resolveThirdParty('harfbuzz', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYENV');
      expectVarHelp(err);
      return true;
    });
  });

  it('(D3-4) unknown D3 dep name throws ETHIRDPARTYPIN, never silent pass', () => {
    const { dir, pinsPath } = makeD3Fixture();
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('d3-build-');
    assert.throws(() => resolveThirdParty('harfbuzz-ng', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYPIN');
      expectVarHelp(err);
      return true;
    });
  });

  it('(D3-5) tampered D3 tarball throws ETHIRDPARTYHASH', () => {
    const { dir, pinsPath } = makeD3Fixture();
    fs.appendFileSync(path.join(dir, 'icu4c.tar.gz'), 'tampered-bytes');
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('d3-build-');
    assert.throws(() => resolveThirdParty('icu4c', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYHASH');
      expectVarHelp(err);
      return true;
    });
  });

  it('(D3-6) missing D3 tarball (host-build artifact absent) fails closed', () => {
    // Resolver-layer half of "ICU without host build fails closed": the
    // pin names a tarball the dir does not hold, so resolution throws
    // ETHIRDPARTYHASH instead of configuring against half-present inputs.
    // The CMake FATAL_ERROR half is covered structurally in
    // tools/deps/tests/test_d3_wiring.py.
    const { dir, pinsPath } = makeD3Fixture();
    fs.rmSync(path.join(dir, 'icu4c.tar.gz'));
    process.env[VAR_NAME] = dir;
    const buildTree = mkTmp('d3-build-');
    assert.throws(() => resolveThirdParty('icu4c', { pinsPath, buildTree }), (err) => {
      assert.equal(err.code, 'ETHIRDPARTYHASH');
      expectVarHelp(err);
      return true;
    });
  });
});

describe('T3 — lazy gate holds for D3 (AC-BUILD-079)', () => {
  it('(D3-7) no dep required with env unset still succeeds', () => {
    delete process.env[VAR_NAME];
    const buildTree = mkTmp('d3-build-');
    const pinsPath = path.join(mkTmp('d3-pins-'), 'pins.json');
    fs.writeFileSync(pinsPath, '{}');
    assert.equal(requiresThirdParty(''), false);
    assert.equal(resolveThirdParty('', { pinsPath, buildTree }).dir, buildTree);
  });
});
