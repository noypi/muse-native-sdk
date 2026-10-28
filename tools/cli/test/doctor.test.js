'use strict';
// T2 — FR-DX-008 / AC-DX-033 / TC-DX-033 (Batch 1).
// Hermetic: node:test + node:assert/strict only. Fake env only —
// process.env is overridden in-process per test and restored in teardown.
// No secrets, no real toolchain probes, no live device.
// Interface under test (plan verbatim):
//   runDoctor() => Array<{name: string, ok: boolean, found?: string,
//     remediation?: string}> exported from tools/cli/lib/doctor.js;
//   every failing entry names owning subsystem + spec id and gives an
//   exact fix command.
// ASSUMPTION: BUILD-001 facade untouched — doctor probes env-style inputs
// (MUSE_NDK_HOME, MUSE_SDK_HOME, etc.) via process.env, never real paths.
// ASSUMPTION: the 8 check names are: ndk, sdk, platform-tools, emulator,
// licenses, jdk, device, driver.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'doctor.js');

const EXPECTED = [
  'ndk',
  'sdk',
  'platform-tools',
  'emulator',
  'licenses',
  'jdk',
  'device',
  'driver',
];

const SAVED = {};
const TOUCHED = [
  'MUSE_NDK_HOME', 'MUSE_SDK_HOME', 'MUSE_PLATFORM_TOOLS',
  'MUSE_EMULATOR', 'MUSE_LICENSES', 'MUSE_JDK_HOME',
  'MUSE_DEVICE', 'MUSE_DRIVER',
];

function load() {
  return require(LIB);
}

beforeEach(() => {
  delete require.cache[LIB];
  for (const k of TOUCHED) {
    SAVED[k] = Object.prototype.hasOwnProperty.call(process.env, k) ? process.env[k] : undefined;
    delete process.env[k];
  }
});

afterEach(() => {
  for (const k of TOUCHED) {
    if (SAVED[k] === undefined) delete process.env[k];
    else process.env[k] = SAVED[k];
  }
  delete require.cache[LIB];
});

function allPresentEnv() {
  for (const k of TOUCHED) process.env[k] = `/fake/${k.toLowerCase()}`;
}

describe('doctor runDoctor (T2)', () => {
  it('returns all 8 check categories', () => {
    allPresentEnv();
    const { runDoctor } = load();
    const rows = runDoctor();
    assert.ok(Array.isArray(rows));
    const names = rows.map((r) => r.name);
    for (const n of EXPECTED) assert.ok(names.includes(n), `missing check ${n}`);
    assert.equal(rows.length, 8);
  });

  it('all-ok env yields ok:true entries with found detail', () => {
    allPresentEnv();
    const { runDoctor } = load();
    const rows = runDoctor();
    for (const r of rows) {
      assert.equal(r.ok, true);
      assert.equal(typeof r.name, 'string');
    }
  });

  it('empty env yields failing entries each with remediation fix command', () => {
    const { runDoctor } = load();
    const rows = runDoctor();
    const fails = rows.filter((r) => !r.ok);
    assert.ok(fails.length > 0, 'expected failures with empty env');
    for (const f of fails) {
      assert.equal(typeof f.remediation, 'string');
      assert.ok(f.remediation.length > 0, `${f.name} must carry a fix command`);
    }
  });

  it('each failure names subsystem + spec id with exact fix command', () => {
    const { runDoctor } = load();
    const rows = runDoctor();
    for (const f of rows.filter((r) => !r.ok)) {
      const text = `${f.name} ${f.remediation}`;
      assert.match(text, /AC-DX-033|DX-008/i, `${f.name} must name spec id`);
      assert.match(String(f.remediation), /\S+\s+\S+/);
    }
  });

  it('missing NDK names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_NDK_HOME;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'ndk');
    assert.equal(row.ok, false);
    assert.match(String(row.remediation), /ndk/i);
  });

  it('missing SDK names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_SDK_HOME;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'sdk');
    assert.equal(row.ok, false);
    assert.match(String(row.remediation), /sdk/i);
  });

  it('missing platform-tools names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_PLATFORM_TOOLS;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'platform-tools');
    assert.equal(row.ok, false);
    assert.ok(String(row.remediation).length > 0);
  });

  it('missing emulator names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_EMULATOR;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'emulator');
    assert.equal(row.ok, false);
    assert.ok(String(row.remediation).length > 0);
  });

  it('missing licenses names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_LICENSES;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'licenses');
    assert.equal(row.ok, false);
    assert.ok(String(row.remediation).length > 0);
  });

  it('missing JDK names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_JDK_HOME;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'jdk');
    assert.equal(row.ok, false);
    assert.ok(String(row.remediation).length > 0);
  });

  it('missing device names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_DEVICE;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'device');
    assert.equal(row.ok, false);
    assert.ok(String(row.remediation).length > 0);
  });

  it('missing driver names fix command', () => {
    allPresentEnv();
    delete process.env.MUSE_DRIVER;
    const { runDoctor } = load();
    const row = runDoctor().find((r) => r.name === 'driver');
    assert.equal(row.ok, false);
    assert.ok(String(row.remediation).length > 0);
  });
});
