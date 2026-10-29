'use strict';
// Batch 2 (FR-PACKAGING-004, AC-PACKAGING-009 / TC-PACKAGING-009).
// Scope: T3 zipalign+debug-sign lane with verify gate (lib/signing.js) and
// T4 CLI/config signing options (lib/cli.js + lib/config.js). Must RED until
// lib/signing.js exists and lib/cli.js + lib/config.js gain the signing keys.
// Hermetic: node:test + node:assert/strict + stdlib only. Tool invocation is
// stubbed by monkey-patching `node:child_process` (+ `/promises` variant) in
// this process — no real zipalign/apksigner/keytool/SDK, no network/device.
// Secrets are fake placeholders only ('STOREPASS-FAKE-*'); assertions prove
// the passphrase never leaks into errors or stderr.
// ASSUMPTION A-S1 (contract genuinely silent): signApk shells out via
// `node:child_process` (execFile/spawn family); the stub covers exec,
// execFile, spawn (+Sync variants, +promises.execFile) and records the tool
// basename + args of every call, so align-then-sign-then-verify order is
// observable no matter which spawn helper the coder picks.
// ASSUMPTION A-S2: env names mirror the archs/packaging pattern as
// MUSE_NATIVE_KEYSTORE / MUSE_NATIVE_KEY_ALIAS / MUSE_NATIVE_STOREPASS.
// ASSUMPTION A-S3: `--keystore <path>` / `--key-alias <name>` are space-form
// value flags like the existing VALUE_FLAGS pattern; `--key-alias` maps to
// the `keyAlias` key via the existing kebab-to-camelCase rule.
// ASSUMPTION A-S4: plan T4 Interface names the layer map `layers`; the live
// resolveConfig returns it as `winningLayer` (established T2 convention) —
// tests assert `winningLayer` and the contract records the alias.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { validateOptions, OptionError } = require('../lib/cli.js');
const { resolveConfig, ConfigError } = require('../lib/config.js');

function loadSigning() {
  // Lazy so T3 red is per-test MODULE_NOT_FOUND (missing behavior), while
  // T4 tests still execute and fail on their own assertions.
  return require('../lib/signing.js');
}

// --- hermetic child_process stub -------------------------------------------
// programs: array consumed per tool call, one entry per expected call:
//   { ok: true } or { ok: false, stderr: '<tool stderr>' }.
// Returns { calls, restore }. calls[i] = { tool, args } where tool is the
// basename of the invoked binary ('zipalign' | 'apksigner' | ...).
function stubTools(programs) {
  const cp = require('node:child_process');
  const queue = Array.isArray(programs) ? programs.slice() : [];
  const calls = [];
  const originals = {};
  for (const key of ['execFile', 'execFileSync', 'spawnSync', 'exec', 'execSync', 'spawn']) {
    if (typeof cp[key] === 'function') originals[key] = cp[key];
  }
  let promisesMod = null;
  let promisesOriginal = null;
  try {
    promisesMod = require('node:child_process/promises');
    if (promisesMod && typeof promisesMod.execFile === 'function') {
      promisesOriginal = promisesMod.execFile;
    }
  } catch (_) {
    promisesMod = null;
  }

  function record(cmd, args) {
    const tool = path.basename(String(cmd).split(' ')[0]);
    calls.push({ tool, args: Array.isArray(args) ? args.slice() : [] });
  }
  function next() {
    return queue.length ? queue.shift() : { ok: true };
  }
  function syncResult(behavior) {
    if (behavior.ok) return { status: 0, stdout: '', stderr: '' };
    const err = new Error(`stubbed failure`);
    err.status = 1;
    err.stderr = behavior.stderr || 'stubbed tool failure';
    err.stdout = '';
    throw err;
  }

  cp.execFile = function fakeExecFile(cmd, args, opts, cb) {
    if (typeof args === 'function') { cb = args; args = []; }
    if (typeof opts === 'function') { cb = opts; opts = undefined; }
    record(cmd, args);
    const behavior = next();
    if (typeof cb === 'function') {
      setImmediate(() => {
        if (behavior.ok) cb(null, '', '');
        else {
          const err = new Error(behavior.stderr || 'stubbed tool failure');
          err.stderr = behavior.stderr || 'stubbed tool failure';
          cb(err, '', behavior.stderr || '');
        }
      });
      return {};
    }
    // promisified (util.promisify) path: return a thenable.
    return behavior.ok
      ? Promise.resolve({ stdout: '', stderr: '' })
      : Promise.reject(Object.assign(new Error(behavior.stderr || 'stubbed tool failure'), {
        stderr: behavior.stderr || 'stubbed tool failure',
      }));
  };
  cp.execFileSync = function fakeExecFileSync(cmd, args) {
    record(cmd, args);
    const behavior = next();
    if (!behavior.ok) {
      const err = new Error(behavior.stderr || 'stubbed tool failure');
      err.status = 1;
      err.stderr = behavior.stderr || 'stubbed tool failure';
      throw err;
    }
    return Buffer.from('');
  };
  cp.spawnSync = function fakeSpawnSync(cmd, args) {
    record(cmd, args);
    return syncResult(next());
  };
  cp.exec = function fakeExec(cmd, opts, cb) {
    if (typeof opts === 'function') { cb = opts; opts = undefined; }
    record(cmd, []);
    const behavior = next();
    if (typeof cb === 'function') {
      setImmediate(() => {
        if (behavior.ok) cb(null, '', '');
        else {
          const err = new Error(behavior.stderr || 'stubbed tool failure');
          err.stderr = behavior.stderr || 'stubbed tool failure';
          cb(err, '', behavior.stderr || '');
        }
      });
    }
    return { on() { return this; } };
  };
  cp.execSync = function fakeExecSync(cmd) {
    record(cmd, []);
    const behavior = next();
    if (!behavior.ok) {
      const err = new Error(behavior.stderr || 'stubbed tool failure');
      err.stderr = behavior.stderr || 'stubbed tool failure';
      throw err;
    }
    return Buffer.from('');
  };
  cp.spawn = function fakeSpawn(cmd, args) {
    record(cmd, args);
    const behavior = next();
    const listeners = {};
    setImmediate(() => {
      const code = behavior.ok ? 0 : 1;
      (listeners.close || []).forEach((fn) => fn(code));
      (listeners.exit || []).forEach((fn) => fn(code));
    });
    return {
      on(ev, fn) { (listeners[ev] = listeners[ev] || []).push(fn); return this; },
      stderr: { on() { return this; } },
      stdout: { on() { return this; } },
    };
  };
  if (promisesMod && promisesOriginal) {
    promisesMod.execFile = function fakePromisesExecFile(cmd, args) {
      record(cmd, args);
      const behavior = next();
      return behavior.ok
        ? Promise.resolve({ stdout: '', stderr: '' })
        : Promise.reject(Object.assign(new Error(behavior.stderr || 'stubbed tool failure'), {
          stderr: behavior.stderr || 'stubbed tool failure',
        }));
    };
  }

  return {
    calls,
    restore() {
      for (const key of Object.keys(originals)) cp[key] = originals[key];
      if (promisesMod && promisesOriginal) promisesMod.execFile = promisesOriginal;
    },
  };
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

const FAKE_APK = Buffer.from('FAKE-APK-BYTES-T3');
const FAKE_OPTS = {
  apkBytes: FAKE_APK,
  keystorePath: path.join('test-assets', 'debug.keystore'),
  alias: 'androiddebugkey',
  storepass: 'STOREPASS-FAKE-1',
  buildToolsDir: path.join('test-assets', 'build-tools'),
};

describe('T3 signApk resolves aligned+signed bytes (AC-PACKAGING-009)', () => {
  it('resolves a non-empty Buffer distinct from unsigned acceptance', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const { signApk } = loadSigning();
      const out = await signApk({ ...FAKE_OPTS });
      assert.ok(Buffer.isBuffer(out), 'must resolve a Buffer');
      assert.ok(out.length > 0, 'signed bytes must be non-empty');
    } finally {
      stub.restore();
    }
  });

  it('invokes align then sign then verify in order', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const { signApk } = loadSigning();
      await signApk({ ...FAKE_OPTS });
      const tools = stub.calls.map((c) => c.tool);
      assert.ok(tools.length >= 3, `expected >=3 tool calls, saw: ${tools.join(',')}`);
      const alignIdx = tools.indexOf('zipalign');
      assert.ok(alignIdx !== -1, 'must shell out to zipalign');
      const signIdx = tools.findIndex(
        (t, i) => i > alignIdx && /apksigner/.test(t),
      );
      assert.ok(signIdx !== -1, 'must shell out to apksigner sign after zipalign');
      const verifyIdx = tools.findIndex((t, i) => i > signIdx && /apksigner/.test(t));
      assert.ok(verifyIdx !== -1, 'must run apksigner verify after sign');
    } finally {
      stub.restore();
    }
  });

  it('sign step references the debug keystore alias', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const { signApk } = loadSigning();
      await signApk({ ...FAKE_OPTS });
      const flat = stub.calls.map((c) => `${c.tool} ${c.args.join(' ')}`).join('\n');
      assert.match(flat, /androiddebugkey/, 'alias must reach the sign invocation');
    } finally {
      stub.restore();
    }
  });

  it('rejects naming zipalign when align fails', async () => {
    const stub = stubTools([{ ok: false, stderr: 'zipalign: bad alignment' }]);
    try {
      const { signApk } = loadSigning();
      await assert.rejects(signApk({ ...FAKE_OPTS }), (err) => {
        assert.match(String(err && err.message), /^signing: zipalign failed: /);
        assert.match(String(err && err.message), /bad alignment/);
        return true;
      });
    } finally {
      stub.restore();
    }
  });

  it('rejects naming apksigner when sign fails', async () => {
    const stub = stubTools([{ ok: true }, { ok: false, stderr: 'apksigner: keystore tampered' }]);
    try {
      const { signApk } = loadSigning();
      await assert.rejects(signApk({ ...FAKE_OPTS }), (err) => {
        assert.match(String(err && err.message), /^signing: apksigner failed: /);
        assert.match(String(err && err.message), /keystore tampered/);
        return true;
      });
    } finally {
      stub.restore();
    }
  });

  it('fails closed when verify fails — never accepts unsigned return', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: false, stderr: 'apksigner: verify failed' }]);
    try {
      const { signApk } = loadSigning();
      await assert.rejects(signApk({ ...FAKE_OPTS }), (err) => {
        assert.match(String(err && err.message), /^signing: apksigner failed: /);
        assert.match(String(err && err.message), /verify failed/);
        return true;
      });
    } finally {
      stub.restore();
    }
  });

  it('rejects fail-closed on empty apkBytes', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const { signApk } = loadSigning();
      await assert.rejects(signApk({ ...FAKE_OPTS, apkBytes: Buffer.alloc(0) }), (err) => {
        assert.match(String(err && err.message), /^signing: /);
        return true;
      });
      assert.equal(stub.calls.length, 0, 'must not shell out for empty input');
    } finally {
      stub.restore();
    }
  });

  it('never includes the storepass in rejection messages', async () => {
    const secret = 'STOREPASS-FAKE-9z9z';
    const stub = stubTools([{ ok: true }, { ok: false, stderr: 'apksigner: boom' }]);
    try {
      const { signApk } = loadSigning();
      const err = await signApk({ ...FAKE_OPTS, storepass: secret }).then(
        () => null,
        (e) => e,
      );
      assert.ok(err instanceof Error, 'must reject');
      assert.doesNotMatch(String(err.message), new RegExp(secret.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')));
    } finally {
      stub.restore();
    }
  });

  it('never writes the storepass to stderr', async () => {
    const secret = 'STOREPASS-FAKE-7q7q';
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    const writes = [];
    const origWrite = process.stderr.write;
    process.stderr.write = function fakeWrite(chunk, enc, cb) {
      writes.push(String(chunk));
      return origWrite.call(this, chunk, enc, cb);
    };
    try {
      const { signApk } = loadSigning();
      await signApk({ ...FAKE_OPTS, storepass: secret });
      const logged = writes.join('');
      assert.ok(!logged.includes(secret), 'stderr must not contain the passphrase');
    } finally {
      process.stderr.write = origWrite;
      stub.restore();
    }
  });
});

describe('T4 validateOptions accepts signing flags', () => {
  it('accepts --keystore <path>', () => {
    validateOptions(['build', 'app', '--keystore', 'test-assets/debug.keystore']);
  });

  it('accepts --key-alias <name>', () => {
    validateOptions(['build', 'app', '--key-alias', 'androiddebugkey']);
  });

  it('rejects missing --keystore value naming the option', () => {
    assert.throws(() => validateOptions(['build', 'app', '--keystore']), (err) => {
      assert.ok(err instanceof OptionError);
      assert.match(String(err.option), /--keystore/);
      assert.equal(err.accepted, 'a value');
      return true;
    });
  });

  it('rejects --keystore=<path> equals form', () => {
    assert.throws(
      () => validateOptions(['build', 'app', '--keystore=test-assets/debug.keystore']),
      (err) => {
        assert.ok(err instanceof OptionError);
        return true;
      },
    );
  });
});

describe('T4 resolveConfig signing keys + layers (AC-PACKAGING-009)', () => {
  it('defaults keyAlias to androiddebugkey with layer default', () => {
    const dir = mkTmp('muse-pack-sign-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.keyAlias, 'androiddebugkey');
    assert.equal(winningLayer.keyAlias, 'default');
  });

  it('defaults keystore to a bundled debug keystore path with layer default', () => {
    const dir = mkTmp('muse-pack-sign-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(typeof config.keystore, 'string');
    assert.ok(config.keystore.length > 0, 'default keystore must be a non-empty path');
    assert.equal(winningLayer.keystore, 'default');
  });

  it('CLI --keystore/--key-alias override file values with layer cli', () => {
    const dir = mkTmp('muse-pack-sign-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'keystore: file-debug.keystore\nkeyAlias: filealias\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig(
      ['--keystore', 'cli-debug.keystore', '--key-alias', 'clialias'],
      {},
      dir,
    );
    assert.equal(config.keystore, 'cli-debug.keystore');
    assert.equal(config.keyAlias, 'clialias');
    assert.equal(winningLayer.keystore, 'cli');
    assert.equal(winningLayer.keyAlias, 'cli');
  });

  it('env layer resolves with layer env', () => {
    const dir = mkTmp('muse-pack-sign-');
    const { config, winningLayer } = resolveConfig(
      [],
      { MUSE_NATIVE_KEYSTORE: 'env-debug.keystore', MUSE_NATIVE_KEY_ALIAS: 'envalias' },
      dir,
    );
    assert.equal(config.keystore, 'env-debug.keystore');
    assert.equal(config.keyAlias, 'envalias');
    assert.equal(winningLayer.keystore, 'env');
    assert.equal(winningLayer.keyAlias, 'env');
  });

  it('CLI beats env beats file for signing keys with named winning layer', () => {
    const dir = mkTmp('muse-pack-sign-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'keystore: file-debug.keystore\n', 'utf8');
    const fromEnv = resolveConfig([], { MUSE_NATIVE_KEYSTORE: 'env-debug.keystore' }, dir);
    assert.equal(fromEnv.config.keystore, 'env-debug.keystore');
    assert.equal(fromEnv.winningLayer.keystore, 'env');
    const fromCli = resolveConfig(
      ['--keystore', 'cli-debug.keystore'],
      { MUSE_NATIVE_KEYSTORE: 'env-debug.keystore' },
      dir,
    );
    assert.equal(fromCli.config.keystore, 'cli-debug.keystore');
    assert.equal(fromCli.winningLayer.keystore, 'cli');
  });

  it('unknown signing value type throws ConfigError (fail-closed)', () => {
    const dir = mkTmp('muse-pack-sign-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'keystore: 42\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /keystore/);
      assert.match(String(err.message), /expected string/);
      return true;
    });
  });

  it('invalid packaging/archs behavior unchanged (TC-PACKAGING-005/006 regression)', () => {
    const dir = mkTmp('muse-pack-sign-');
    assert.throws(() => resolveConfig(['--packaging', 'universal'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /fat, split, both/);
      return true;
    });
    assert.throws(() => validateOptions(['build', 'app', '--packaging', 'universal']), (err) => {
      assert.ok(err instanceof OptionError);
      return true;
    });
  });
});
