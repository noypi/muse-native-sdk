'use strict';
// FR-PACKAGING-006 batch 1 (T1 engine-lib inputs + T2 ELF arch gate).
// Scope: AC-PACKAGING-013 (named CLI/config engine-lib inputs, fail-closed
// missing arch at package) and AC-PACKAGING-014 (ELF arch-correctness per
// slot ABI). Later batches (T3 dex, T4 sign) are out of scope.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/config.js, lib/cli.js, lib/stages.js (and lib/apk.js fallback for
// validateElfArch) in-process; file layer uses fs.mkdtemp dirs; env is a
// plain object argument; no network, no secrets, no live services, no
// Android SDK.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig, ConfigError } = require('../lib/config.js');
const { validateOptions, OptionError } = require('../lib/cli.js');
const { runStages } = require('../lib/stages.js');

const ALL_FOUR = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];
const MACHINE_FOR_ABI = {
  'arm64-v8a': 183,
  'armeabi-v7a': 40,
  x86_64: 62,
  x86: 3,
};

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
  buf[1] = 0x45; // 'E' (already written by 'ELF'; kept explicit for clarity)
  buf.writeUInt16LE(machine, 18);
  if (extraBytes) return Buffer.concat([buf, Buffer.from(extraBytes)]);
  return buf;
}

function loadValidateElfArch() {
  for (const rel of ['./stages.js', '../lib/stages.js', './apk.js', '../lib/apk.js']) {
    try {
      const mod = require(rel);
      if (mod && typeof mod.validateElfArch === 'function') return mod.validateElfArch;
    } catch (_) {
      // Try the next candidate below.
    }
  }
  try {
    const stages = require('../lib/stages.js');
    if (stages && typeof stages.validateElfArch === 'function') return stages.validateElfArch;
  } catch (_) {
    // Fall through to the apk.js candidate below.
  }
  const apk = require('../lib/apk.js');
  return apk.validateElfArch;
}

// Minimal stored-entry extractor for buildApk ZIPs (local headers in order).
function zipEntryData(buf, targetName) {
  assert.ok(Buffer.isBuffer(buf));
  let p = 0;
  while (p + 30 <= buf.length) {
    if (buf.readUInt32LE(p) !== 0x04034b50) break;
    const compSize = buf.readUInt32LE(p + 18);
    const nameLen = buf.readUInt16LE(p + 26);
    const extraLen = buf.readUInt16LE(p + 28);
    const name = buf.toString('utf8', p + 30, p + 30 + nameLen);
    const dataStart = p + 30 + nameLen + extraLen;
    if (name === targetName) return buf.subarray(dataStart, dataStart + compSize);
    p = dataStart + compSize;
  }
  return null;
}

function fatPath(appDir) {
  return path.join(appDir, 'dist', 'app-fat.apk');
}

// ---- T1: named engine-lib inputs (AC-PACKAGING-013, TC-PACKAGING-013) ----

describe('T1 resolveConfig — engineLibs default lookup (AC-PACKAGING-013)', () => {
  it('default resolution maps every selected arch to a file path', () => {
    const dir = mkTmp('muse-pack-eng-');
    const { config } = resolveConfig([], {}, dir);
    assert.ok(config.engineLibs && typeof config.engineLibs === 'object');
    for (const abi of ALL_FOUR) {
      assert.equal(typeof config.engineLibs[abi], 'string');
      assert.ok(config.engineLibs[abi].length > 0, `default path for ${abi}`);
    }
  });
});

describe('T1 resolveConfig — --engine-lib repeatable CLI flag (AC-PACKAGING-013)', () => {
  it('repeatable --engine-lib <abi=path> entries land in engineLibs', () => {
    const dir = mkTmp('muse-pack-eng-');
    const { config } = resolveConfig(
      ['--engine-lib', 'arm64-v8a=/tmp/a.so', '--engine-lib', 'x86_64=/tmp/b.so'],
      {},
      dir,
    );
    assert.equal(config.engineLibs['arm64-v8a'], '/tmp/a.so');
    assert.equal(config.engineLibs.x86_64, '/tmp/b.so');
  });

  it('CLI --engine-lib wins over the file engine_libs map', () => {
    const dir = mkTmp('muse-pack-eng-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'name: t1\nversion: 0.0.1\nengine_libs: arm64-v8a=/file/a.so\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig(
      ['--engine-lib', 'arm64-v8a=/cli/a.so'],
      {},
      dir,
    );
    assert.equal(config.engineLibs['arm64-v8a'], '/cli/a.so');
    assert.equal(winningLayer.engineLibs, 'cli');
  });
});

describe('T1 cli.js — --engine-lib is an accepted option (AC-PACKAGING-013)', () => {
  it('validateOptions accepts repeatable --engine-lib <abi=path>', () => {
    assert.doesNotThrow(() => validateOptions(
      ['build', 'my-app', '--engine-lib', 'arm64-v8a=/tmp/a.so'],
    ));
    assert.doesNotThrow(() => validateOptions(
      ['build', 'my-app', '--engine-lib', 'arm64-v8a=/tmp/a.so', '--engine-lib', 'x86_64=/tmp/b.so'],
    ));
  });
});

describe('T1 runStages package — file-backed libs + fail-closed missing arch (AC-PACKAGING-013)', () => {
  it('emitted APK lib entry bytes equal the resolved input file', async () => {
    const dir = mkTmp('muse-pack-eng-');
    writeStageApp(dir);
    const soBytes = makeElf(183, 'RUNTIME-arm64');
    const soPath = path.join(dir, 'libRuntime-arm64.so');
    fs.writeFileSync(soPath, soBytes);
    const { config } = resolveConfig(
      ['--archs', 'arm64-v8a', '--engine-lib', `arm64-v8a=${soPath}`],
      {},
      dir,
    );
    const signMark = Buffer.from('MARKED-BY-FAKE-SIGNAPK-T1', 'utf8');
    const results = await runStages('package', {
      config,
      appDir: dir,
      _signApk: async (buf) => Buffer.concat([Buffer.from(buf), signMark]),
    });
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, true);
    assert.ok(fs.existsSync(fatPath(dir)), 'dist/app-fat.apk must exist');
    const apkBytes = fs.readFileSync(fatPath(dir));
    assert.ok(apkBytes.includes(signMark), 'fat APK must pass through the signed lane');
    const entry = zipEntryData(apkBytes, 'lib/arm64-v8a/libRuntime.so');
    assert.ok(entry, 'fat APK holds lib/arm64-v8a/libRuntime.so');
    assert.deepEqual(Buffer.from(entry), soBytes);
  });

  it("selected arch with no resolvable library fails naming the arch and writes no APK", async () => {
    const dir = mkTmp('muse-pack-eng-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a'], {}, dir);
    const results = await runStages(
      'package',
      { config: { ...config, engineLibs: {} }, appDir: dir },
    );
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, false);
    assert.match(results[0].error.message, /missing native lib for arch 'arm64-v8a'/);
    assert.ok(!fs.existsSync(fatPath(dir)), 'no APK on fail-closed run');
  });
});

// ---- T2: ELF arch gate (AC-PACKAGING-014, TC-PACKAGING-014) ----

describe('T2 validateElfArch — correct machine per ABI passes silently (AC-PACKAGING-014)', () => {
  for (const abi of ALL_FOUR) {
    it(`${abi} accepts e_machine ${MACHINE_FOR_ABI[abi]}`, () => {
      const validateElfArch = loadValidateElfArch();
      assert.equal(typeof validateElfArch, 'function');
      validateElfArch(makeElf(MACHINE_FOR_ABI[abi]), abi);
    });
  }
});

describe('T2 validateElfArch — mismatch fails naming abi + machines (AC-PACKAGING-014)', () => {
  it('x86-64 bytes in the arm64-v8a slot throw arch mismatch naming abi and machines', () => {
    const validateElfArch = loadValidateElfArch();
    assert.throws(
      () => validateElfArch(makeElf(62), 'arm64-v8a'),
      (err) => {
        assert.match(String(err && err.message), /arch mismatch for arm64-v8a/);
        assert.match(String(err && err.message), /183/);
        assert.match(String(err && err.message), /62/);
        return true;
      },
    );
  });

  it('non-ELF input throws (never silently accepted)', () => {
    const validateElfArch = loadValidateElfArch();
    assert.throws(
      () => validateElfArch(Buffer.from('NOT-AN-ELF-OBJECT', 'utf8'), 'x86_64'),
      (err) => {
        assert.match(String(err && err.message), /arch mismatch for x86_64/);
        return true;
      },
    );
  });

  it('truncated input shorter than the ELF header throws', () => {
    const validateElfArch = loadValidateElfArch();
    assert.throws(
      () => validateElfArch(Buffer.from([0x7f, 0x45, 0x4c]), 'x86'),
      (err) => {
        assert.match(String(err && err.message), /arch mismatch for x86/);
        return true;
      },
    );
  });
});

// Security — input validation at the arch gate (GR-05 R5.3/R5.4).
describe('Security — validateElfArch rejects hostile and oversized input (AC-PACKAGING-014)', () => {
  it("hostile abi '../../evil' never passes, even with a valid header", () => {
    const validateElfArch = loadValidateElfArch();
    assert.throws(
      () => validateElfArch(makeElf(183), '../../evil'),
      (err) => {
        assert.match(String(err && err.message), /\.\.\/\.\.\/evil/);
        return true;
      },
    );
  });

  it('oversized non-ELF buffer throws instead of being scanned unsafely', () => {
    const validateElfArch = loadValidateElfArch();
    const big = Buffer.alloc(1024 * 1024, 0x41);
    assert.throws(
      () => validateElfArch(big, 'arm64-v8a'),
      (err) => {
        assert.match(String(err && err.message), /arch mismatch for arm64-v8a/);
        return true;
      },
    );
  });
});

// ---- T3: compiled classes.dex (AC-PACKAGING-015, TC-PACKAGING-015) ----
// Hermetic: node:test + node:assert/strict + stdlib only. compileDex is
// driven through fake javac/d8 executables written into a tmp
// buildToolsDir plus a stub android.jar file — no real SDK, no network.
// dex.js is required lazily inside each case so the missing module fails
// only these cases (red) without breaking the T1/T2 cases above.

const DEX_MAGIC_4 = 'dex\n';

function loadCompileDex() {
  const mod = require('../lib/dex.js');
  assert.equal(typeof mod.compileDex, 'function', 'dex.js must export compileDex');
  return mod.compileDex;
}

function writeExecutable(dir, name, script) {
  const p = path.join(dir, name);
  fs.writeFileSync(p, script, { mode: 0o755 });
  return p;
}

// Fake javac: exits 0, touches a .class next to every .java arg.
const FAKE_JAVAC = '#!/bin/sh\n'
  + 'for a in "$@"; do\n'
  + '  case "$a" in *.java) touch "${a%.java}.class" ;; esac\n'
  + 'done\nexit 0\n';

// Fake d8: writes dex-magic bytes to every plausible dex output — any arg
// ending in .dex, and any --output/--output= target (file, or dir holding
// classes.dex). Exits 0.
const FAKE_D8 = '#!/bin/sh\n'
  + 'dexhdr=$(printf \'dex\\n035\\0\')\n'
  + 'out=""\nprev=""\n'
  + 'for a in "$@"; do\n'
  + '  if [ "$prev" = "--output" ]; then out="$a"; prev=""; continue; fi\n'
  + '  case "$a" in\n'
  + '    --output) prev="--output" ;;\n'
  + '    --output=*) out="${a#--output=}" ;;\n'
  + '    *.dex) printf "%s" "$dexhdr-padding-by-fake-d8" > "$a" ;;\n'
  + '  esac\n'
  + 'done\n'
  + 'if [ -n "$out" ]; then\n'
  + '  case "$out" in\n'
  + '    *.dex) printf "%s" "$dexhdr-padding-by-fake-d8" > "$out" ;;\n'
  + '    *) mkdir -p "$out"; printf "%s" "$dexhdr-padding-by-fake-d8" > "$out/classes.dex" ;;\n'
  + '  esac\n'
  + 'fi\n'
  + 'exit 0\n';

// Failing d8: names itself on stderr, exits 1.
const FAILING_D8 = '#!/bin/sh\necho "d8: fake failure compiling classes" >&2\nexit 1\n';

function writeFakeToolchain(opts) {
  const root = mkTmp('muse-pack-dex-');
  const btDir = path.join(root, 'build-tools');
  fs.mkdirSync(btDir, { recursive: true });
  writeExecutable(btDir, 'javac', FAKE_JAVAC);
  writeExecutable(btDir, 'd8', opts && opts.d8Script ? opts.d8Script : FAKE_D8);
  const androidJar = path.join(root, 'android.jar');
  if (!opts || opts.androidJar !== false) fs.writeFileSync(androidJar, 'STUB-ANDROID-JAR');
  const srcDir = path.join(root, 'src');
  fs.mkdirSync(srcDir, { recursive: true });
  const javaPath = path.join(srcDir, 'MainActivity.java');
  fs.writeFileSync(javaPath, 'package com.muse.host;\npublic class MainActivity {}\n', 'utf8');
  return { root, btDir, androidJar, javaPath };
}

describe('T3 compileDex — interface and dex magic (AC-PACKAGING-015)', () => {
  it('dex.js exports compileDex(javaSources, opts) returning dex bytes starting with magic dex\\n', async () => {
    const compileDex = loadCompileDex();
    const { btDir, androidJar, javaPath } = writeFakeToolchain();
    const out = await compileDex([javaPath], { androidJar, buildToolsDir: btDir });
    assert.ok(Buffer.isBuffer(out), 'must return a Buffer');
    assert.equal(out.subarray(0, 4).toString('utf8'), DEX_MAGIC_4);
  });

  it('empty javaSources fails closed naming the missing piece', async () => {
    const compileDex = loadCompileDex();
    const { btDir, androidJar } = writeFakeToolchain();
    await assert.rejects(
      () => compileDex([], { androidJar, buildToolsDir: btDir }),
      (err) => {
        assert.match(String(err && err.message), /java sources|no sources|missing.*source/i);
        return true;
      },
    );
  });

  it('missing android.jar fails closed naming the missing piece', async () => {
    const compileDex = loadCompileDex();
    const { btDir, javaPath } = writeFakeToolchain({ androidJar: false });
    await assert.rejects(
      () => compileDex([javaPath], { androidJar: path.join(path.dirname(btDir), 'android.jar'), buildToolsDir: btDir }),
      (err) => {
        assert.match(String(err && err.message), /android\.jar|androidJar/i);
        return true;
      },
    );
  });

  it('missing buildToolsDir fails closed naming the missing piece', async () => {
    const compileDex = loadCompileDex();
    const { androidJar, javaPath, root } = writeFakeToolchain();
    await assert.rejects(
      () => compileDex([javaPath], { androidJar, buildToolsDir: path.join(root, 'no-such-build-tools') }),
      (err) => {
        assert.match(String(err && err.message), /build-?tools|javac|d8|toolchain/i);
        return true;
      },
    );
  });

  it("toolchain failure throws 'dex compile failed: ...' naming the tool and source", async () => {
    const compileDex = loadCompileDex();
    const { btDir, androidJar, javaPath } = writeFakeToolchain({ d8Script: FAILING_D8 });
    await assert.rejects(
      () => compileDex([javaPath], { androidJar, buildToolsDir: btDir }),
      (err) => {
        assert.match(String(err && err.message), /dex compile failed:/);
        assert.match(String(err && err.message), /d8/);
        assert.match(String(err && err.message), /MainActivity\.java/);
        return true;
      },
    );
  });

  it('nonexistent java source fails naming the source, before any dist/ write', async () => {
    const compileDex = loadCompileDex();
    const { btDir, androidJar, root } = writeFakeToolchain();
    const missing = path.join(root, 'src', 'Nope.java');
    await assert.rejects(
      () => compileDex([missing], { androidJar, buildToolsDir: btDir }),
      (err) => {
        assert.match(String(err && err.message), /Nope\.java/);
        return true;
      },
    );
    assert.ok(!fs.existsSync(path.join(root, 'dist')), 'no dist/ on fail-closed run');
  });
});

// Security — hostile toolchain input must never execute (GR-05 R5.3/R5.4).
describe('Security — compileDex rejects hostile sources without invoking the toolchain (AC-PACKAGING-015)', () => {
  it("path-traversal source '../../evil.java' throws and the toolchain marker is never created", async () => {
    const compileDex = loadCompileDex();
    const { btDir, androidJar, root } = writeFakeToolchain();
    const marker = path.join(root, 'TOOLCHAIN-EXECUTED');
    // Tripwire: any toolchain execution drops this marker.
    writeExecutable(btDir, 'javac', `#!/bin/sh\ntouch "${marker}"\nexit 0\n`);
    writeExecutable(btDir, 'd8', `#!/bin/sh\ntouch "${marker}"\nexit 0\n`);
    await assert.rejects(
      () => compileDex([path.join(root, '..', '..', 'evil.java')], { androidJar, buildToolsDir: btDir }),
      (err) => {
        assert.match(String(err && err.message), /evil|\.\./);
        return true;
      },
    );
    assert.ok(!fs.existsSync(marker), 'toolchain must not run for hostile input');
  });

  it('oversized source list is rejected instead of being compiled unsafely', async () => {
    const compileDex = loadCompileDex();
    const { btDir, androidJar, javaPath } = writeFakeToolchain();
    const huge = new Array(100001).fill(javaPath);
    await assert.rejects(
      () => compileDex(huge, { androidJar, buildToolsDir: btDir }),
      (err) => {
        assert.match(String(err && err.message), /too many|oversized|limit/i);
        return true;
      },
    );
  });
});

describe('T3 packageStage — dex wiring and fail-closed edges (AC-PACKAGING-015)', () => {
  it('ctx._hostInputs.dexBytes override lands in classes.dex with dex magic (hermetic seam kept)', async () => {
    const dir = mkTmp('muse-pack-dex-');
    writeStageApp(dir);
    const { config } = resolveConfig(['--archs', 'arm64-v8a'], {}, dir);
    const magicDex = Buffer.concat([Buffer.from('dex\n035\0', 'binary'), Buffer.from('hermetic-seam')]);
    const signMark = Buffer.from('MARKED-BY-FAKE-SIGNAPK-T3', 'utf8');
    const results = await runStages('package', {
      config,
      appDir: dir,
      _hostInputs: {
        dexBytes: magicDex,
        resourcesArsc: Buffer.from('ARSC-SEAM', 'utf8'),
        nativeLibs: [{ abi: 'arm64-v8a', name: 'libRuntime.so', bytes: makeElf(183, 'RUNTIME') }],
      },
      _signApk: async (buf) => Buffer.concat([Buffer.from(buf), signMark]),
    });
    assert.equal(results.length, 1);
    assert.equal(results[0].ok, true);
    const apkBytes = fs.readFileSync(fatPath(dir));
    assert.ok(apkBytes.includes(signMark), 'APK must pass through the signed lane');
    const entry = zipEntryData(apkBytes, 'classes.dex');
    assert.ok(entry, 'APK must contain classes.dex');
    assert.equal(Buffer.from(entry).subarray(0, 4).toString('utf8'), DEX_MAGIC_4);
    assert.deepEqual(Buffer.from(entry), magicDex);
  });

  it('default package never ships DEX-STAGE-FALLBACK text: real dex magic or fail-closed naming the missing piece', async () => {
    const dir = mkTmp('muse-pack-dex-');
    writeStageApp(dir);
    const soBytes = makeElf(183, 'RUNTIME-arm64');
    const soPath = path.join(dir, 'libRuntime-arm64.so');
    fs.writeFileSync(soPath, soBytes);
    const { config } = resolveConfig(
      ['--archs', 'arm64-v8a', '--engine-lib', `arm64-v8a=${soPath}`],
      {},
      dir,
    );
    const results = await runStages('package', { config, appDir: dir });
    assert.equal(results.length, 1);
    if (results[0].ok) {
      const apkBytes = fs.readFileSync(fatPath(dir));
      const entry = zipEntryData(apkBytes, 'classes.dex');
      assert.ok(entry, 'APK must contain classes.dex');
      assert.ok(
        !Buffer.from(entry).includes('DEX-STAGE-FALLBACK'),
        'fallback text must be gone once wired to compileDex',
      );
      assert.equal(Buffer.from(entry).subarray(0, 4).toString('utf8'), DEX_MAGIC_4);
    } else {
      assert.match(
        String(results[0].error && results[0].error.message),
        /java|source|android\.jar|androidJar|build-?tools|javac|d8|toolchain|sdk|dex/i,
        'fail-closed error must name the missing piece',
      );
      assert.ok(!fs.existsSync(fatPath(dir)), 'no APK on fail-closed run');
    }
  });
});

// Security — LOW-1: engine-lib keys must reject prototype pollution (GR-05 R5.3).
describe('Security — engineLibs rejects __proto__/constructor/prototype keys (LOW-1)', () => {
  it("CLI --engine-lib __proto__=<path> throws ConfigError with 'rejected dangerous key'", () => {
    const dir = mkTmp('muse-pack-eng-');
    assert.throws(
      () => resolveConfig(['--engine-lib', '__proto__=/tmp/evil.so'], {}, dir),
      (err) => {
        assert.ok(err instanceof ConfigError);
        assert.match(String(err && err.message), /rejected dangerous key/);
        assert.match(String(err && err.message), /__proto__/);
        return true;
      },
    );
  });

  it("env string-form __proto__=<path> throws ConfigError with 'rejected dangerous key'", () => {
    const dir = mkTmp('muse-pack-eng-');
    assert.throws(
      () => resolveConfig([], { MUSE_NATIVE_ENGINE_LIBS: '__proto__=/tmp/evil.so' }, dir),
      (err) => {
        assert.ok(err instanceof ConfigError);
        assert.match(String(err && err.message), /rejected dangerous key/);
        return true;
      },
    );
  });

  it('file string-form engine_libs __proto__ entry throws ConfigError with rejected dangerous key', () => {
    const dir = mkTmp('muse-pack-eng-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'name: t1\nversion: 0.0.1\nengine_libs: __proto__=/tmp/evil.so\n',
      'utf8',
    );
    assert.throws(
      () => resolveConfig([], {}, dir),
      (err) => {
        assert.ok(err instanceof ConfigError);
        assert.match(String(err && err.message), /rejected dangerous key/);
        return true;
      },
    );
  });

  it('legitimate arm64-v8a=<path> control still resolves', () => {
    const dir = mkTmp('muse-pack-eng-');
    const { config } = resolveConfig(
      ['--engine-lib', 'arm64-v8a=/tmp/a.so'],
      {},
      dir,
    );
    assert.equal(config.engineLibs['arm64-v8a'], '/tmp/a.so');
  });
});
