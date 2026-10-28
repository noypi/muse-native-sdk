'use strict';
// T6 — FR-DX-008 / AC-DX-038 / TC-DX-038 (Batch 2).
// Test-only harness: executes the T2 doctor path and the T4 dispatch path
// headlessly. No new production surface — this file asserts behavior of the
// already-committed tools/cli/lib/doctor.js and
// tools/cli/lib/deeplink-tester.js, plus the T5 error presentation
// (tools/cli/lib/cli-errors.js) applied to the doctor failure.
// Hermetic: node:test + node:assert/strict only. Toolchain breakage is a
// fake env (MUSE_NDK_HOME unset in-process, restored in teardown); route
// resolution uses the optional __setNavResolver hook with an opaque stub.
// No secrets, no config, no device, no live services.
// Interface under test (plan verbatim):
//   none — test-only harness executing the T2 doctor path and T4 dispatch
//   path; asserts Fail remediation strings and routeError structure; no new
//   production surface.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB_DIR = path.resolve(__dirname, '..', 'lib');
const DOCTOR = path.join(LIB_DIR, 'doctor.js');
const DEEPLINK = path.join(LIB_DIR, 'deeplink-tester.js');
const CLI_ERRORS = path.join(LIB_DIR, 'cli-errors.js');

const TOUCHED = [
  'MUSE_NDK_HOME', 'MUSE_SDK_HOME', 'MUSE_PLATFORM_TOOLS',
  'MUSE_EMULATOR', 'MUSE_LICENSES', 'MUSE_JDK_HOME',
  'MUSE_DEVICE', 'MUSE_DRIVER',
];
const SAVED = {};

function fresh(modPath) {
  delete require.cache[modPath];
  return require(modPath);
}

beforeEach(() => {
  for (const m of [DOCTOR, DEEPLINK, CLI_ERRORS]) delete require.cache[m];
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
  for (const m of [DOCTOR, DEEPLINK, CLI_ERRORS]) delete require.cache[m];
});

function healthyEnvExceptNdk() {
  for (const k of TOUCHED) {
    if (k !== 'MUSE_NDK_HOME') process.env[k] = `/fake/${k.toLowerCase()}`;
  }
}

describe('CLI acceptance: misconfigured env (T6 / TC-DX-038 doctor path)', () => {
  it('unset NDK path fails the ndk check naming the component + exact fix', () => {
    healthyEnvExceptNdk();
    const { runDoctor } = fresh(DOCTOR);
    const rows = runDoctor();
    const row = rows.find((r) => r.name === 'ndk');
    assert.ok(row, 'doctor must report an ndk check');
    assert.equal(row.ok, false);
    assert.match(String(row.remediation), /ndk/i, 'Fail must name the missing component');
    assert.match(String(row.remediation), /AC-DX-033|DX-008/i, 'Fail must name the spec id');
    assert.match(String(row.remediation), /\S+\s+\S+/, 'Fail must carry an exact fix command');
  });

  it('all other checks still pass with only NDK broken', () => {
    healthyEnvExceptNdk();
    const { runDoctor } = fresh(DOCTOR);
    const rows = runDoctor().filter((r) => r.name !== 'ndk');
    for (const r of rows) assert.equal(r.ok, true, `${r.name} should pass`);
  });

  it('doctor failure formats through formatFailure with command+cause+action', () => {
    healthyEnvExceptNdk();
    const { runDoctor } = fresh(DOCTOR);
    const { formatFailure } = fresh(CLI_ERRORS);
    const row = runDoctor().find((r) => r.name === 'ndk');
    const out = formatFailure({ command: 'doctor', cause: String(row.remediation) });
    assert.equal(out.command, 'doctor');
    assert.ok(out.cause.includes('ndk') || out.cause.includes('NDK'));
    assert.equal(typeof out.action, 'string');
    assert.ok(out.action.length > 0);
    assert.ok(!/\n\s*at\s/.test(JSON.stringify(out)), 'formatted error must not leak a stack');
  });
});

describe('CLI acceptance: unknown route (T6 / TC-DX-038 dispatch path)', () => {
  it('unknown route yields a structured symbolized routeError', () => {
    const mod = fresh(DEEPLINK);
    if (typeof mod.__setNavResolver === 'function') {
      mod.__setNavResolver((route) => {
        if (route === 'muse://home') return { ok: true };
        return { ok: false, symbol: 'NAV_UNKNOWN_ROUTE', hint: 'check route spelling' };
      });
    }
    const res = mod.dispatchRoute('muse://no-such-screen');
    assert.equal(res.status, 'routeError');
    assert.equal(res.route, 'muse://no-such-screen');
    assert.equal(typeof res.cause, 'string');
    assert.ok(res.cause.length > 0);
    assert.match(String(res.cause), /NAV_|unknown route|AC-DX-035/i);
    assert.equal(typeof res.action, 'string');
    assert.ok(res.action.length > 0);
  });

  it('routeError never leaks a raw stack', () => {
    const mod = fresh(DEEPLINK);
    if (typeof mod.__setNavResolver === 'function') {
      mod.__setNavResolver(() => ({ ok: false, symbol: 'NAV_UNKNOWN_ROUTE', hint: 'nope' }));
    }
    const res = mod.dispatchRoute('muse://no-such-screen');
    const text = JSON.stringify(res);
    assert.ok(!/\n\s*at\s/.test(text), 'must not contain a raw stack trace');
    assert.ok(!/node_modules\/.+\.js:\d+/.test(text), 'must not contain frame paths');
  });
});
