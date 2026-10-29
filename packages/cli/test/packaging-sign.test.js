'use strict';
// FR-PACKAGING-006 batch 3 — T4 packageStage align+sign (AC-PACKAGING-016 /
// TC-PACKAGING-016).
// Scope: `packageStage` (lib/stages.js, via lib/build.js) signs EVERY emitted
// APK (fat + each split) with the resolved debug keystore through the real
// `signApk` lane (lib/signing.js); `runStages('package', ctx)` fails closed
// naming the tool when align+sign fails; the release AAB stays unsigned.
// MUST RED until stages.js replaces the unsigned pass-through default
// (`signed()` returning input when no `ctx._signApk`) with the real signApk
// call, keeping `ctx._signApk` as the hermetic-test override.
// Hermetic: node:test + node:assert/strict + stdlib only. Tool invocation is
// stubbed by monkey-patching `node:child_process` (+ `/promises` variant) in
// this process — no real zipalign/apksigner/keytool/SDK, no keystore file, no
// network/device. Secrets are fake placeholders only ('STOREPASS-FAKE-T4-*');
// assertions prove the passphrase never leaks into errors. ELF inputs are
// synthetic (magic + e_machine), matching packaging-engine.test.js.
// ASSUMPTION A-T4a (contract genuinely silent on byte shape): the real
// signApk success marker is the existing T3 seam suffix `-aligned-signed`
// (signing.js returns Buffer.concat([apkBytes, '-aligned-signed'])); tests
// assert the suffix is present on every APK, i.e. bytes are signed, not the
// unsigned pass-through.
// ASSUMPTION A-T4b: signApk shells out via `node:child_process` execFile
// (callback form); the stub covers exec/execFile/spawn (+Sync, +promises)
// and records the tool basename + args, so align-then-sign-then-verify order
// per APK is observable no matter which helper the coder picks.
// ASSUMPTION A-T4c: the debug storepass resolves from config.storepass
// (`MUSE_NATIVE_STOREPASS` env in these tests); the default keystore/keyAlias
// come from resolveConfig defaults (committed batch-2 behavior).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig } = require('../lib/config.js');
const { runStages } = require('../lib/stages.js');

// --- hermetic child_process stub (copied convention) ------------------------
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
    const err = new Error('stubbed failure');
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

function writeStageApp(appDir) {
  fs.mkdirSync(path.join(appDir, 'src'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: stage-app\nversion: 0.0.1\n',
    'utf8',
  );
  fs.writeFileSync(
    path.join(appDir, 'src', 'App.tsx'),
    [
      "import { View, Text } from '@muse/native';",
      'export default function App() {',
      '  return (<View><Text>hi</Text></View>);',
      '}',
      '',
    ].join('\n'),
    'utf8',
  );
}

// Minimal synthetic ELF: magic + e_machine (u16LE at offset 18).
function makeElf(machine, extraBytes) {
  const buf = Buffer.alloc(64);
  buf[0] = 0x7f;
  buf.write('ELF', 1);
  buf[1] = 0x45;
  buf.writeUInt16LE(machine, 18);
  if (extraBytes) return Buffer.concat([buf, Buffer.from(extraBytes)]);
  return buf;
}

const MACHINE_FOR_ABI = { 'arm64-v8a': 183, x86_64: 62 };
const TWO_ARCHS = ['arm64-v8a', 'x86_64'];
const FAKE_STOREPASS = 'STOREPASS-FAKE-T4-1';
const SIGN_SUFFIX = '-aligned-signed';

function hostInputs(archs) {
  return {
    dexBytes: Buffer.concat([Buffer.from('dex\n035\0', 'binary'), Buffer.from('hermetic-seam-t4')]),
    resourcesArsc: Buffer.from('ARSC-SEAM-T4', 'utf8'),
    nativeLibs: archs.map((abi) => ({
      abi,
      name: 'libRuntime.so',
      bytes: makeElf(MACHINE_FOR_ABI[abi], `RUNTIME-${abi}`),
    })),
  };
}

function resolveWithStorepass(argv, dir) {
  return resolveConfig(argv, { MUSE_NATIVE_STOREPASS: FAKE_STOREPASS }, dir);
}

function fatPath(appDir) {
  return path.join(appDir, 'dist', 'app-fat.apk');
}

function splitPath(appDir, arch) {
  return path.join(appDir, 'dist', `app-${arch}.apk`);
}

function aabPath(appDir) {
  return path.join(appDir, 'dist', 'app.aab');
}

function apkFilesPresent(appDir) {
  const dist = path.join(appDir, 'dist');
  let names = [];
  try {
    names = fs.readdirSync(dist);
  } catch (_) {
    return [];
  }
  return names.filter((n) => n.endsWith('.apk'));
}

describe('T4 fat APK is aligned+signed via the real signApk lane (AC-PACKAGING-016)', () => {
  it('package shells out to zipalign then apksigner sign then verify for the fat APK', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(['--archs', TWO_ARCHS.join(',')], dir);
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results.length, 1);
      assert.equal(results[0].ok, true);
      const tools = stub.calls.map((c) => c.tool);
      assert.ok(tools.length >= 3, `expected >=3 tool calls, saw: ${tools.join(',')}`);
      const alignIdx = tools.indexOf('zipalign');
      assert.ok(alignIdx !== -1, 'must shell out to zipalign (no unsigned pass-through)');
      const signIdx = tools.findIndex((t, i) => i > alignIdx && /apksigner/.test(t));
      assert.ok(signIdx !== -1, 'must shell out to apksigner sign after zipalign');
      const verifyIdx = tools.findIndex((t, i) => i > signIdx && /apksigner/.test(t));
      assert.ok(verifyIdx !== -1, 'must run apksigner verify after sign');
    } finally {
      stub.restore();
    }
  });

  it('fat APK bytes are signed bytes, not the unsigned pass-through', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(['--archs', TWO_ARCHS.join(',')], dir);
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results[0].ok, true);
      const bytes = fs.readFileSync(fatPath(dir));
      assert.ok(
        bytes.includes(Buffer.from(SIGN_SUFFIX)),
        'fat APK must carry the signApk signature suffix (unsigned pass-through is gone)',
      );
    } finally {
      stub.restore();
    }
  });
});

describe('T4 every APK in the writes set is signed — fat + each split (AC-PACKAGING-016)', () => {
  it('packaging both signs fat plus every split APK', async () => {
    // 3 APKs x (zipalign + sign + verify) = 9 tool calls.
    const stub = stubTools(Array.from({ length: 9 }, () => ({ ok: true })));
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(
        ['--archs', TWO_ARCHS.join(','), '--packaging', 'both'],
        dir,
      );
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results[0].ok, true);
      const expected = [fatPath(dir), ...TWO_ARCHS.map((a) => splitPath(dir, a))];
      for (const p of expected) {
        assert.ok(fs.existsSync(p), `${path.basename(p)} must exist`);
        const bytes = fs.readFileSync(p);
        assert.ok(
          bytes.includes(Buffer.from(SIGN_SUFFIX)),
          `${path.basename(p)} must carry the signApk signature suffix`,
        );
      }
      const tools = stub.calls.map((c) => c.tool);
      assert.equal(
        tools.filter((t) => t === 'zipalign').length,
        3,
        `each of the 3 APKs must be aligned, saw: ${tools.join(',')}`,
      );
      assert.ok(
        tools.filter((t) => /apksigner/.test(t)).length >= 6,
        `each of the 3 APKs must be signed+verified, saw: ${tools.join(',')}`,
      );
    } finally {
      stub.restore();
    }
  });

  it('packaging split signs every split APK', async () => {
    const stub = stubTools(Array.from({ length: 6 }, () => ({ ok: true })));
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(
        ['--archs', TWO_ARCHS.join(','), '--packaging', 'split'],
        dir,
      );
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results[0].ok, true);
      assert.ok(!fs.existsSync(fatPath(dir)), 'no fat APK in split mode');
      for (const arch of TWO_ARCHS) {
        const bytes = fs.readFileSync(splitPath(dir, arch));
        assert.ok(
          bytes.includes(Buffer.from(SIGN_SUFFIX)),
          `app-${arch}.apk must carry the signApk signature suffix`,
        );
      }
    } finally {
      stub.restore();
    }
  });
});

describe('T4 signer failure fails closed naming the tool with no APK written', () => {
  it('apksigner sign failure rejects at package naming apksigner with zero APKs on disk', async () => {
    const stub = stubTools([
      { ok: true },
      { ok: false, stderr: 'apksigner: keystore was tampered with' },
    ]);
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(['--archs', TWO_ARCHS.join(',')], dir);
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results.length, 1);
      assert.equal(results[0].ok, false);
      assert.match(
        String(results[0].error && results[0].error.message),
        /zipalign|apksigner|signing/,
        'package failure must name the failing tool',
      );
      assert.deepEqual(apkFilesPresent(dir), [], 'no APK may be written when signing fails');
    } finally {
      stub.restore();
    }
  });

  it('verify failure fails closed — never accepts unsigned return', async () => {
    const stub = stubTools([
      { ok: true },
      { ok: true },
      { ok: false, stderr: 'apksigner: verify failed: no verified signatures' },
    ]);
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(['--archs', TWO_ARCHS.join(',')], dir);
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results[0].ok, false);
      assert.match(
        String(results[0].error && results[0].error.message),
        /zipalign|apksigner|signing/,
        'verify failure must fail closed naming the tool',
      );
      assert.deepEqual(apkFilesPresent(dir), [], 'no APK may be written when verify fails');
    } finally {
      stub.restore();
    }
  });

  it('failure message never includes the storepass', async () => {
    const secret = 'STOREPASS-FAKE-T4-9z9z';
    const stub = stubTools([
      { ok: true },
      { ok: false, stderr: 'apksigner: boom' },
    ]);
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveConfig(
        ['--archs', TWO_ARCHS.join(',')],
        { MUSE_NATIVE_STOREPASS: secret },
        dir,
      );
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
      });
      assert.equal(results[0].ok, false);
      assert.ok(
        !String(results[0].error && results[0].error.message).includes(secret),
        'error must not leak the passphrase',
      );
    } finally {
      stub.restore();
    }
  });
});

describe('T4 ctx._signApk hermetic override is preserved', () => {
  it('override marks every APK and the real tools stay untouched', async () => {
    const stub = stubTools(Array.from({ length: 9 }, () => ({ ok: true })));
    const seen = [];
    const mark = 'MARKED-BY-FAKE-SIGNAPK';
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(
        ['--archs', TWO_ARCHS.join(','), '--packaging', 'both'],
        dir,
      );
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
        _signApk: async (buf) => {
          assert.ok(Buffer.isBuffer(buf), 'override receives Buffer input');
          seen.push(buf.length);
          return Buffer.concat([buf, Buffer.from(mark)]);
        },
      });
      assert.equal(results[0].ok, true);
      assert.equal(seen.length, 3, 'override must be invoked once per emitted APK');
      for (const p of [fatPath(dir), ...TWO_ARCHS.map((a) => splitPath(dir, a))]) {
        assert.ok(fs.readFileSync(p).includes(Buffer.from(mark)), `${path.basename(p)} signed by override`);
      }
      assert.equal(stub.calls.length, 0, 'real tools must not run when the override is set');
    } finally {
      stub.restore();
    }
  });

  it('failing override fails closed naming the tool with no APK written', async () => {
    const stub = stubTools([{ ok: true }, { ok: true }, { ok: true }]);
    try {
      const dir = mkTmp('muse-pack-sign-');
      writeStageApp(dir);
      const { config } = resolveWithStorepass(['--archs', TWO_ARCHS.join(',')], dir);
      const results = await runStages('package', {
        config,
        appDir: dir,
        _hostInputs: hostInputs(TWO_ARCHS),
        _signApk: async () => {
          throw new Error('apksigner: fake override boom');
        },
      });
      assert.equal(results[0].ok, false);
      assert.match(
        String(results[0].error && results[0].error.message),
        /zipalign|apksigner|signing/,
        'override failure must name the tool',
      );
      assert.deepEqual(apkFilesPresent(dir), [], 'no APK may be written when the override fails');
    } finally {
      stub.restore();
    }
  });
});

describe('T4 release AAB path stays unsigned', () => {
  it('AAB carries no signature marker while APKs are signed', async () => {
    const mark = 'MARKED-BY-FAKE-SIGNAPK-AAB';
    const dir = mkTmp('muse-pack-sign-');
    writeStageApp(dir);
    const { config } = resolveWithStorepass(['--archs', TWO_ARCHS.join(',')], dir);
    const results = await runStages('package', {
      config: { ...config, release: true },
      appDir: dir,
      _hostInputs: hostInputs(TWO_ARCHS),
      _signApk: async (buf) => Buffer.concat([buf, Buffer.from(mark)]),
    });
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(aabPath(dir)), 'dist/app.aab must exist in release');
    assert.ok(
      !fs.readFileSync(aabPath(dir)).includes(Buffer.from(mark)),
      'AAB must stay unsigned (no APK signature marker)',
    );
    assert.ok(
      fs.readFileSync(fatPath(dir)).includes(Buffer.from(mark)),
      'fat APK is still signed in the release run',
    );
  });
});
