'use strict';
// T3 — Play delivery fakes (NFR-BUILD-005 / AC-BUILD-005 mount half; consumes
// NFR-BUILD-010 delivery-shape names only). Depends on T2.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only. All
// fixtures live under os.tmpdir(); no network, no Play Console, no device, no
// emulator, no secrets, no live services. The "device" is a fake tmpdir pack
// stand-in; progress is deterministic and driven by polling monitorPack.
// Fixture syntax ASSUMPTIONs (recorded in unit-tests/T3.md): pack names are
// obvious dummy strings (`fake-pack-not-real`); the failing-first-attempt
// fixture is keyed by a pack name containing `retry` (first download attempt
// fails, retry-with-resume reaches `ready`); error retry metadata is the
// `retries` property on the thrown `delivery-failed` Error.
// Red state: toolchain/cli/playDelivery.js does not exist yet, so every case
// below fails until T3 lands (MODULE_NOT_FOUND = missing behavior, not a
// malformed test).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('./playDelivery.js');
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
    delete require.cache[require.resolve('./playDelivery.js')];
  } catch { /* not yet implemented — red state */ }
});

function assertDeliveryFailed(fn) {
  let err = null;
  try {
    fn();
  } catch (e) {
    err = e;
  }
  assert.ok(err instanceof Error, 'must throw an Error');
  assert.match(err.message, /^delivery-failed: \S.+/, 'message must be delivery-failed: <reason>');
  assert.equal(typeof err.retries, 'number', 'thrown error must carry numeric retry metadata { retries }');
  assert.ok(err.retries >= 0, 'retries must be >= 0');
  assert.doesNotMatch(err.message, /fake-secret|token|BEGIN PRIVATE KEY/);
  return err;
}

// Drive request→poll to a terminal state within a bounded number of steps.
// Deterministic: no timers, no wall-clock waits — each monitorPack call is one
// synchronous step.
function pollToTerminal(monitorPack, handle, maxSteps = 20) {
  let last = null;
  for (let i = 0; i < maxSteps; i += 1) {
    last = monitorPack(handle);
    assert.ok(['pending', 'downloading', 'ready', 'failed'].includes(last.state), `unknown state: ${last.state}`);
    assert.equal(typeof last.pct, 'number', 'pct must be a number');
    assert.ok(last.pct >= 0 && last.pct <= 100, `pct out of range: ${last.pct}`);
    if (last.state === 'ready' || last.state === 'failed') return last;
  }
  assert.fail(`pack ${handle} did not reach a terminal state within ${maxSteps} steps`);
  return last;
}

describe('T3 — requestPack contract shape', () => {
  it('returns { handle: string } with a non-empty handle', () => {
    const { requestPack } = loadImpl();
    const result = requestPack('fake-pack-not-real');
    assert.equal(typeof result.handle, 'string');
    assert.ok(result.handle.length > 0, 'handle must be non-empty');
  });

  it('two requests return distinct handles', () => {
    const { requestPack } = loadImpl();
    const first = requestPack('fake-pack-not-real');
    const second = requestPack('fake-pack-not-real');
    assert.notEqual(first.handle, second.handle);
  });

  it('an empty pack name throws delivery-failed with retry metadata (input validation)', () => {
    const { requestPack } = loadImpl();
    assertDeliveryFailed(() => requestPack(''));
  });

  it('a non-string pack name throws delivery-failed with retry metadata (input validation)', () => {
    const { requestPack } = loadImpl();
    assertDeliveryFailed(() => requestPack(42));
    assertDeliveryFailed(() => requestPack(null));
  });
});

describe('T3 — monitorPack deterministic progress', () => {
  it('a requested pack reaches ready with pct 100', () => {
    const { requestPack, monitorPack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    const terminal = pollToTerminal(monitorPack, handle);
    assert.equal(terminal.state, 'ready');
    assert.equal(terminal.pct, 100);
  });

  it('progress pct is monotonic non-decreasing across polls', () => {
    const { requestPack, monitorPack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    let prev = -1;
    for (let i = 0; i < 20; i += 1) {
      const cur = monitorPack(handle);
      assert.ok(cur.pct >= prev, `pct regressed: ${prev} -> ${cur.pct}`);
      prev = cur.pct;
      if (cur.state === 'ready' || cur.state === 'failed') break;
    }
  });

  it('an unknown handle throws delivery-failed with retry metadata', () => {
    const { monitorPack } = loadImpl();
    assertDeliveryFailed(() => monitorPack('no-such-handle-not-real'));
  });

  it('a non-string handle throws delivery-failed with retry metadata', () => {
    const { monitorPack } = loadImpl();
    assertDeliveryFailed(() => monitorPack(null));
  });
});

describe('T3 — retry-with-resume (failing-first-attempt fixture)', () => {
  it('the retry fixture pack reaches ready after one simulated failure', () => {
    const { requestPack, monitorPack } = loadImpl();
    // ASSUMPTION: pack names containing `retry` select the failing-first-attempt
    // fixture (first download attempt ends `failed`; retry-with-resume reaches
    // `ready`). The retry is driven by re-requesting the same pack name.
    const first = requestPack('fake-pack-retry-not-real');
    const failed = pollToTerminal(monitorPack, first.handle);
    assert.equal(failed.state, 'failed', 'first attempt must fail to exercise retry-with-resume');
    const second = requestPack('fake-pack-retry-not-real');
    const terminal = pollToTerminal(monitorPack, second.handle);
    assert.equal(terminal.state, 'ready');
    assert.equal(terminal.pct, 100);
  });

  it('a failed attempt surfaces retries metadata on the thrown error', () => {
    const { requestPack, mountPack, monitorPack } = loadImpl();
    const { handle } = requestPack('fake-pack-retry-not-real');
    const terminal = pollToTerminal(monitorPack, handle);
    assert.equal(terminal.state, 'failed');
    const err = assertDeliveryFailed(() => mountPack(handle));
    assert.ok(err.retries >= 1, `failed attempt must report retries >= 1, got ${err.retries}`);
  });
});

describe('BUG-BUILD-001 — monitorPack first-poll pending sequence', () => {
  it('ok mode polls pending/0 then downloading/50 then ready/100', () => {
    const { requestPack, monitorPack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    assert.deepEqual(monitorPack(handle), { state: 'pending', pct: 0 });
    assert.deepEqual(monitorPack(handle), { state: 'downloading', pct: 50 });
    assert.deepEqual(monitorPack(handle), { state: 'ready', pct: 100 });
  });

  it('retry fixture first attempt polls pending/0 then downloading/50 then failed/50', () => {
    const { requestPack, monitorPack } = loadImpl();
    const { handle } = requestPack('fake-pack-retry-not-real');
    assert.deepEqual(monitorPack(handle), { state: 'pending', pct: 0 });
    assert.deepEqual(monitorPack(handle), { state: 'downloading', pct: 50 });
    assert.deepEqual(monitorPack(handle), { state: 'failed', pct: 50 });
  });
});

describe('T3 — mountPack contract shape', () => {
  it('a ready pack mounts to an existing mountPoint directory', () => {
    const { requestPack, monitorPack, mountPack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    const terminal = pollToTerminal(monitorPack, handle);
    assert.equal(terminal.state, 'ready');
    const result = mountPack(handle);
    assert.equal(typeof result.mountPoint, 'string');
    assert.ok(fs.existsSync(result.mountPoint), `mountPoint must exist: ${result.mountPoint}`);
    assert.ok(fs.statSync(result.mountPoint).isDirectory(), 'mountPoint must be a directory');
  });

  it('mounting a pack that is not ready throws delivery-failed with retry metadata', () => {
    const { requestPack, mountPack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    // Fresh handle has not reached `ready` yet: mounting early must fail.
    assertDeliveryFailed(() => mountPack(handle));
  });

  it('an unknown handle throws delivery-failed with retry metadata', () => {
    const { mountPack } = loadImpl();
    assertDeliveryFailed(() => mountPack('no-such-handle-not-real'));
  });

  it('failure messages carry a reason and leak no fixture internals', () => {
    const { mountPack } = loadImpl();
    const err = assertDeliveryFailed(() => mountPack('no-such-handle-not-real'));
    assert.match(err.message, /^delivery-failed: \S.+/);
  });
});

describe('T1 — releasePack caller-owned disposal (BUG-BUILD-002)', () => {
  function countPlayPackDirs() {
    return fs.readdirSync(os.tmpdir()).filter((n) => n.startsWith('play-pack-')).length;
  }

  it('exports releasePack alongside requestPack, monitorPack, mountPack', () => {
    const impl = loadImpl();
    assert.equal(typeof impl.releasePack, 'function', 'releasePack must be exported as a function');
    for (const name of ['requestPack', 'monitorPack', 'mountPack']) {
      assert.equal(typeof impl[name], 'function', `${name} must remain exported`);
    }
  });

  it('a missing handle throws delivery-failed: unknown handle with retry metadata', () => {
    const { releasePack } = loadImpl();
    const err = assertDeliveryFailed(() => releasePack(undefined));
    assert.match(err.message, /^delivery-failed: unknown handle/);
  });

  it('an empty handle throws delivery-failed: unknown handle with retry metadata', () => {
    const { releasePack } = loadImpl();
    const err = assertDeliveryFailed(() => releasePack(''));
    assert.match(err.message, /^delivery-failed: unknown handle/);
  });

  it('a non-string handle throws delivery-failed: unknown handle with retry metadata', () => {
    const { releasePack } = loadImpl();
    assertDeliveryFailed(() => releasePack(null));
    assertDeliveryFailed(() => releasePack(42));
  });

  it('an unknown handle throws delivery-failed: unknown handle with retry metadata', () => {
    const { releasePack } = loadImpl();
    const err = assertDeliveryFailed(() => releasePack('no-such-handle-not-real'));
    assert.match(err.message, /^delivery-failed: unknown handle/);
  });

  it('release removes the mountPoint dir and evicts the handle (monitor/mount throw after)', () => {
    const { requestPack, monitorPack, mountPack, releasePack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    const terminal = pollToTerminal(monitorPack, handle);
    assert.equal(terminal.state, 'ready');
    const { mountPoint } = mountPack(handle);
    assert.ok(fs.existsSync(mountPoint), 'mountPoint must exist before release');
    releasePack(handle);
    assert.ok(!fs.existsSync(mountPoint), 'mountPoint dir must be removed by release');
    const monErr = assertDeliveryFailed(() => monitorPack(handle));
    assert.match(monErr.message, /^delivery-failed: unknown handle/);
    const mntErr = assertDeliveryFailed(() => mountPack(handle));
    assert.match(mntErr.message, /^delivery-failed: unknown handle/);
  });

  it('double-release throws delivery-failed: unknown handle (not a crash, not silent)', () => {
    const { requestPack, monitorPack, releasePack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    pollToTerminal(monitorPack, handle);
    releasePack(handle);
    const err = assertDeliveryFailed(() => releasePack(handle));
    assert.match(err.message, /^delivery-failed: unknown handle/);
  });

  it('release still evicts the map entry when the dir is already gone (best-effort rm)', () => {
    const { requestPack, monitorPack, mountPack, releasePack } = loadImpl();
    const { handle } = requestPack('fake-pack-not-real');
    pollToTerminal(monitorPack, handle);
    const { mountPoint } = mountPack(handle);
    fs.rmSync(mountPoint, { recursive: true, force: true });
    assert.ok(!fs.existsSync(mountPoint));
    releasePack(handle);
    const err = assertDeliveryFailed(() => monitorPack(handle));
    assert.match(err.message, /^delivery-failed: unknown handle/);
  });

  it('N requests driven to terminal then released leave zero net new play-pack-* dirs (single instance)', () => {
    const { requestPack, monitorPack, releasePack } = loadImpl();
    const before = countPlayPackDirs();
    const N = 5;
    const handles = [];
    for (let i = 0; i < N; i += 1) {
      const { handle } = requestPack('fake-pack-not-real');
      handles.push(handle);
    }
    for (const handle of handles) {
      pollToTerminal(monitorPack, handle);
    }
    for (const handle of handles) {
      releasePack(handle);
    }
    const after = countPlayPackDirs();
    assert.equal(after, before, `expected zero net new play-pack-* dirs (before=${before} after=${after})`);
  });

  it('retry fixture still works: first retry-name request fails, re-request reaches ready', () => {
    const { requestPack, monitorPack, releasePack } = loadImpl();
    const first = requestPack('fake-pack-retry-not-real');
    const failed = pollToTerminal(monitorPack, first.handle);
    assert.equal(failed.state, 'failed', 'first attempt must fail to exercise retry-with-resume');
    releasePack(first.handle);
    const second = requestPack('fake-pack-retry-not-real');
    const terminal = pollToTerminal(monitorPack, second.handle);
    assert.equal(terminal.state, 'ready');
    assert.equal(terminal.pct, 100);
    releasePack(second.handle);
  });
});
