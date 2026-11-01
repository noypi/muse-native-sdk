'use strict';
// T4 — pinned hermesc CLI contract gate (NFR-BUILD-020 / AC-BUILD-076).
// Hermetic: node:test + node:assert/strict + stdlib only. No hermesc binary,
// no network, no secrets, no live services. The hermesc collaborator
// (node:child_process execFileSync) is stubbed in-process per test.
// Red state: ./hermescPinGate.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND) until T4 lands.
//
// Interface under test (plan verbatim):
//   checkHermescPin(): { ok: true, version: string } | throws BundleError(code: 'EPINDRIFT', detail: string)
//
// ASSUMPTION (seam): the gate reads the recorded contract from the sibling
//   hermescPin.json (pinned version + exact invocation flags + bytecode
//   target) and obtains the actual CLI contract by shelling
//   `hermesc --version` (plus contract probe) via node:child_process; tests
//   stub that collaborator, never a real binary.
// ASSUMPTION (drift scope): any of {version mismatch, invocation/flags
//   mismatch, bytecode-target mismatch, missing/unreadable pin record,
//   hermesc binary missing} throws BundleError with code 'EPINDRIFT' and a
//   string `detail` naming the drift.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const childProcess = require('node:child_process');

const PIN_PATH = './hermescPinGate.js';

function loadImpl() {
  return require(PIN_PATH);
}

// Stub child_process.execFileSync/execSync in-process; restored in teardown.
const originals = {
  execFileSync: childProcess.execFileSync,
  execSync: childProcess.execSync,
};
let patched = false;
afterEach(() => {
  if (patched) {
    childProcess.execFileSync = originals.execFileSync;
    childProcess.execSync = originals.execSync;
    patched = false;
  }
  delete require.cache[require.resolve(PIN_PATH)];
});

function stubHermesc({ versionOutput, throwOnCall = false } = {}) {
  const canned = Buffer.from(String(versionOutput));
  const boom = new Error('spawn hermesc ENOENT');
  boom.code = 'ENOENT';
  childProcess.execFileSync = (..._args) => {
    if (throwOnCall) throw boom;
    return canned;
  };
  childProcess.execSync = (..._args) => {
    if (throwOnCall) throw boom;
    return canned;
  };
  patched = true;
}

describe('T4 — checkHermescPin pinned version passes (AC-BUILD-076)', () => {
  it('returns { ok:true, version } when the CLI contract matches the pin', () => {
    const { checkHermescPin } = loadImpl();
    // Pinned version is whatever the record holds; stub the CLI to echo it
    // back by first reading the pin record when available, else any version
    // still proves the shape (drift cases below prove comparison happens).
    let pinnedVersion = '0.0.0-pinned-fixture';
    try {
      const pin = require('./hermescPin.json');
      if (pin && typeof pin.version === 'string') pinnedVersion = pin.version;
    } catch (err) {
      void err;
    }
    stubHermesc({ versionOutput: `hermesc version ${pinnedVersion}\n` });
    const result = checkHermescPin();
    assert.equal(result.ok, true);
    assert.equal(typeof result.version, 'string');
    assert.ok(result.version.length > 0);
  });
});

describe('T4 — version/contract drift fails the gate (AC-BUILD-076)', () => {
  it('version drift throws BundleError with code EPINDRIFT', () => {
    const { checkHermescPin, BundleError } = loadImpl();
    stubHermesc({ versionOutput: 'hermesc version 0.0.0-DRIFT-NEVER-PINNED\n' });
    assert.throws(
      () => checkHermescPin(),
      (err) => err instanceof BundleError && err.code === 'EPINDRIFT'
    );
  });

  it('drift error carries a string detail naming the drift', () => {
    const { checkHermescPin, BundleError } = loadImpl();
    stubHermesc({ versionOutput: 'hermesc version 0.0.0-DRIFT-NEVER-PINNED\n' });
    try {
      checkHermescPin();
      assert.fail('expected checkHermescPin to throw EPINDRIFT');
    } catch (err) {
      assert.ok(err instanceof BundleError);
      assert.equal(err.code, 'EPINDRIFT');
      assert.equal(typeof err.detail, 'string');
      assert.ok(err.detail.length > 0);
    }
  });

  it('missing hermesc binary fails closed with EPINDRIFT before any artifact ships', () => {
    const { checkHermescPin, BundleError } = loadImpl();
    stubHermesc({ throwOnCall: true });
    assert.throws(
      () => checkHermescPin(),
      (err) => err instanceof BundleError && err.code === 'EPINDRIFT'
    );
  });

  it('garbage version output fails closed with EPINDRIFT', () => {
    const { checkHermescPin, BundleError } = loadImpl();
    stubHermesc({ versionOutput: 'not-a-hermesc-invocation\n' });
    assert.throws(
      () => checkHermescPin(),
      (err) => err instanceof BundleError && err.code === 'EPINDRIFT'
    );
  });
});

describe('T4 — error shape and secret hygiene', () => {
  it('BundleError is an Error with string code and detail fields', () => {
    const { BundleError } = loadImpl();
    const err = new BundleError('EPINDRIFT', 'drift-fixture-detail');
    assert.ok(err instanceof Error);
    assert.equal(err.code, 'EPINDRIFT');
    assert.equal(err.detail, 'drift-fixture-detail');
  });

  it('drift messages never leak env secrets', () => {
    const { checkHermescPin } = loadImpl();
    process.env.MUSE_HERMESC_TEST_DUMMY = 'top-secret-dummy-value';
    try {
      stubHermesc({ versionOutput: 'hermesc version 0.0.0-DRIFT-NEVER-PINNED\n' });
      assert.throws(
        () => checkHermescPin(),
        (err) => !String((err && (err.detail || err.message)) || '').includes('top-secret-dummy-value')
      );
    } finally {
      delete process.env.MUSE_HERMESC_TEST_DUMMY;
    }
  });
});
