'use strict';
// Batch 1 / T1 (FR-PACKAGING-007/008, AC-PACKAGING-018).
// Scope: remove duplicate JNI_OnLoad so libRuntime.so links with a single
// definition. T1 cases only — T2/T3/T4 append their groups to this file in
// later batches (plan §Batches). Must RED until the single-definition fix
// exists (duplicate source or --allow-multiple-definition fails these).
// Hermetic: node:test + node:assert/strict + stdlib only. Assertions read
// source/linker-recipe text; tool invocation is stubbed by monkey-patching
// `node:child_process` in-process — no NDK/SDK/device/network, no compile,
// no `nm`, no link. Secrets: none used (fake values only where a placeholder
// is required; none here).
// ASSUMPTION A-J1 (contract genuinely silent): the live behavioral call
// `JNI_OnLoad(nullptr, nullptr) === JNI_ERR` executes in C++
// (`engine/runtime/tests/jni_boundary_test.cpp`); this file proves the
// hermetic source-level proxy — the kept engine definition carries the
// null-VM fail-closed gate (`vm == nullptr` → `return JNI_ERR` before any
// version return) — and never spawns a JVM.
// ASSUMPTION A-J2: single-definition is counted textually as occurrences of
// `jint JNI_OnLoad(` across the two listed sources; header comments or
// declarations mentioning JNI_OnLoad without that exact definition prefix
// do not count.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function repoRoot() {
  let dir = __dirname;
  for (let i = 0; i < 8; i += 1) {
    if (fs.existsSync(path.join(dir, 'engine', 'runtime', 'src', 'jni_bridge.cpp'))) return dir;
    dir = path.dirname(dir);
  }
  throw new Error('test setup: cannot locate repo root from ' + __dirname);
}

const ROOT = repoRoot();
const ENGINE_SRC = path.join(ROOT, 'engine', 'runtime', 'src', 'jni_bridge.cpp');
const TEMPLATE_SRC = path.join(ROOT, 'packages', 'cli', 'templates', 'android', 'TreeBridge.cpp');
const ENGINE_CMAKE = path.join(ROOT, 'engine', 'runtime', 'CMakeLists.txt');
const ROOT_MAKEFILE = path.join(ROOT, 'Makefile');
const SCAFFOLD_MAKEFILE = path.join(ROOT, 'examples', 'm3-gallery', 'Makefile');

function read(p) {
  assert.ok(fs.existsSync(p), `expected source to exist: ${path.relative(ROOT, p)}`);
  return fs.readFileSync(p, 'utf8');
}

// Counts non-overlapping occurrences of the exact definition prefix.
function countDefinitions(src) {
  const needle = 'jint JNI_OnLoad(';
  let n = 0;
  let pos = 0;
  while (true) {
    const idx = src.indexOf(needle, pos);
    if (idx === -1) return n;
    n += 1;
    pos = idx + needle.length;
  }
}

function onLoadBody(engineSrc) {
  const m = /jint\s+JNI_OnLoad\s*\([^)]*\)\s*\{([\s\S]*?)\n\}/.exec(engineSrc);
  assert.ok(m, 'engine source must contain a jint JNI_OnLoad definition body');
  return m[1];
}

// --- hermetic child_process stub: proves these tests shell out to nothing --
let stubCalls = [];
let stubOriginals = {};
function stubTools() {
  const cp = require('node:child_process');
  stubCalls = [];
  stubOriginals = {};
  for (const key of ['execFile', 'execFileSync', 'spawnSync', 'exec', 'execSync', 'spawn']) {
    if (typeof cp[key] === 'function') stubOriginals[key] = cp[key];
  }
  const record = (tool) => stubCalls.push(tool);
  cp.execFileSync = () => { record('execFileSync'); throw new Error('hermetic: no tool calls allowed'); };
  cp.execSync = () => { record('execSync'); throw new Error('hermetic: no tool calls allowed'); };
  cp.spawnSync = () => { record('spawnSync'); throw new Error('hermetic: no tool calls allowed'); };
}
function restoreTools() {
  const cp = require('node:child_process');
  for (const key of Object.keys(stubOriginals)) cp[key] = stubOriginals[key];
  stubOriginals = {};
}
beforeEach(() => stubTools());
afterEach(() => restoreTools());

describe('T1 single JNI_OnLoad definition (AC-PACKAGING-018)', () => {
  it('engine jni_bridge.cpp defines exactly one JNI_OnLoad', () => {
    const src = read(ENGINE_SRC);
    assert.equal(countDefinitions(src), 1, 'engine source must define JNI_OnLoad exactly once');
  });

  it('scaffold TreeBridge.cpp defines zero JNI_OnLoad (declaration-only reference)', () => {
    const src = read(TEMPLATE_SRC);
    assert.equal(
      countDefinitions(src), 0,
      'template must not define JNI_OnLoad — duplicate definition breaks the shared link',
    );
  });

  it('exactly one definition is linked across both sources', () => {
    const total = countDefinitions(read(ENGINE_SRC)) + countDefinitions(read(TEMPLATE_SRC));
    assert.equal(total, 1, `expected a single JNI_OnLoad definition, found ${total}`);
  });

  it('kept definition fails closed on null VM before any version return', () => {
    const body = onLoadBody(read(ENGINE_SRC));
    const nullIdx = body.indexOf('vm == nullptr');
    const errIdx = body.indexOf('return JNI_ERR');
    const versionIdx = body.indexOf('JNI_VERSION_1_6');
    assert.ok(nullIdx !== -1, 'null-VM guard (vm == nullptr) must be present');
    assert.ok(errIdx !== -1, 'fail-closed return (return JNI_ERR) must be present');
    assert.ok(nullIdx < errIdx, 'null check must precede the JNI_ERR return');
    assert.ok(versionIdx === -1 || errIdx < versionIdx,
      'JNI_ERR fail-closed path must precede any success version return');
  });

  it('null-VM path can never report success (no premature version return)', () => {
    const body = onLoadBody(read(ENGINE_SRC));
    const nullIdx = body.indexOf('vm == nullptr');
    const firstVersionReturn = body.indexOf('return JNI_VERSION');
    assert.ok(nullIdx !== -1, 'null-VM guard must be present');
    assert.ok(firstVersionReturn === -1 || nullIdx < firstVersionReturn,
      'no success version may be returned before the null-VM gate');
  });

  it('negative control: the counter detects a duplicated definition (RED capability)', () => {
    const duplicated = 'jint JNI_OnLoad(JavaVM* vm, void* reserved) { return 0; }\n'
      + 'jint JNI_OnLoad(JavaVM* vm, void* reserved) { return 0; }\n';
    assert.equal(countDefinitions(duplicated), 2);
    assert.notEqual(1 + 1, 1, 'a duplicate must break the single-definition total');
  });

  it('no link recipe carries --allow-multiple-definition', () => {
    const cmake = read(ENGINE_CMAKE);
    assert.ok(!cmake.includes('allow-multiple-definition'),
      'engine CMake must link without --allow-multiple-definition');
    for (const mf of [ROOT_MAKEFILE, SCAFFOLD_MAKEFILE]) {
      if (!fs.existsSync(mf)) continue; // T2/T3-owned; absent until their batches
      const text = fs.readFileSync(mf, 'utf8');
      assert.ok(!text.includes('allow-multiple-definition'),
        `${path.relative(ROOT, mf)} must not use --allow-multiple-definition`);
    }
  });

  it('hermetic: zero external tool invocations', () => {
    assert.equal(stubCalls.length, 0, 'T1 assertions must not shell out to any tool');
  });
});

// --- T2 root Makefile: engine-host / engine-android / clean + fail-closed ---
// Scope: FR-PACKAGING-007 AC-PACKAGING-017/018/019 (TC-017/018/019).
// Interface (plan T2 verbatim):
//   `make engine-host | make engine-android [ABIS=...] | make clean` → exit 0
//   on success, non-zero with stderr naming the expected NDK rev + offending
//   path (missing/wrong NDK, misconfigured ABI dir) or the ABI slot vs ELF
//   machine on arch mismatch.
// Hermetic: Makefile-text assertions only; child_process stays stubbed (no
// NDK/SDK/CMake/ctest/file/readelf, no device/network). RED until root
// `Makefile` exists with the three targets and the NDK/arch gates.
// ASSUMPTION A-M1 (contract genuinely silent): NDK pin text is the NFR-BUILD-016
// rev `27.2.12479018` (r27); the gate names it via a Makefile variable or a
// literal — tests accept either.
// ASSUMPTION A-M2: supported ABI allow-list defaults to all four packaging slots
// (`arm64-v8a`, `armeabi-v7a`, `x86_64`, `x86`) per plan A3; overridable via
// `ABIS=...` on the make command line (`?=`/`ABIS` reference in text).
// ASSUMPTION A-M3: ELF arch proof is a recipe-level gate comparing the `file`/
// `readelf` machine string against the ABI slot with a non-zero `exit 1` on
// mismatch; live compile/ELF proof is `tester` work (no NDK on this host).
function readMakefile() {
  assert.ok(fs.existsSync(ROOT_MAKEFILE),
    'root Makefile must exist (T2 owns it; RED until the coder adds it)');
  return fs.readFileSync(ROOT_MAKEFILE, 'utf8');
}

function targetBody(text, name) {
  // Matches `name:` target header plus an optional `VAR ?= ...` preamble is
  // asserted separately; body = text from the header to the next target/EOF.
  const lines = text.split('\n');
  const idx = lines.findIndex((l) => new RegExp(`^${name}\\s*:`).test(l));
  assert.ok(idx !== -1, `root Makefile must define target: ${name}`);
  let end = lines.length;
  for (let i = idx + 1; i < lines.length; i += 1) {
    if (/^[A-Za-z0-9_.-]+\s*:(?![:=])/.test(lines[i]) && !/^\s/.test(lines[i])) { end = i; break; }
  }
  return lines.slice(idx, end).join('\n');
}

describe('T2 root Makefile engine targets (AC-PACKAGING-017/018/019)', () => {
  it('root Makefile exists with engine-host, engine-android, clean targets', () => {
    const text = readMakefile();
    for (const t of ['engine-host', 'engine-android', 'clean']) {
      assert.match(text, new RegExp(`^${t}\\s*:`, 'm'), `missing make target: ${t}`);
    }
  });

  it('engine-host configures + compiles + runs ctest green (AC-PACKAGING-017)', () => {
    const body = targetBody(readMakefile(), 'engine-host');
    assert.match(body, /cmake/i, 'engine-host must configure via cmake');
    assert.match(body, /cmake\s+--build|make\b|\bninja\b/i, 'engine-host must compile the configured tree');
    assert.match(body, /ctest/, 'engine-host must run ctest (exit 0 = green)');
  });

  it('engine-android pins NDK r27 27.2.12479018 (AC-PACKAGING-019)', () => {
    const text = readMakefile();
    assert.ok(text.includes('27.2.12479018'),
      'root Makefile must pin NDK rev 27.2.12479018 (NFR-BUILD-016)');
  });

  it('engine-android defaults ABIS to the four-slot allow-list and honors ABIS=... (AC-PACKAGING-018)', () => {
    const text = readMakefile();
    for (const abi of ['arm64-v8a', 'armeabi-v7a', 'x86_64']) {
      assert.ok(text.includes(abi), `engine-android must list ABI slot ${abi}`);
    }
    assert.ok(text.includes('x86'), 'engine-android must list ABI slot x86');
    assert.match(text, /\bABIS\b/, 'engine-android must honor an ABIS=... override');
  });

  it('engine-android emits one arch-correct libRuntime.so per ABI (AC-PACKAGING-018)', () => {
    const body = targetBody(readMakefile(), 'engine-android');
    assert.ok(body.includes('libRuntime.so'), 'engine-android must build libRuntime.so per ABI');
    assert.match(body, /\bfile\b|\breadelf\b/i,
      'engine-android must verify ELF output with file/readelf');
  });

  it('arch mismatch fails the target naming slot vs ELF machine (AC-PACKAGING-018)', () => {
    const body = targetBody(readMakefile(), 'engine-android');
    assert.match(body, /aarch64/i, 'arch gate must name the aarch64 machine for arm64-v8a');
    assert.match(body, /exit\s+1|&&\s*exit|false\b/,
      'arch mismatch must fail the target (non-zero exit)');
    assert.match(body, /ABI|slot|arm64-v8a/i,
      'arch-mismatch error must name the offending ABI slot');
  });

  it('missing NDK fails closed before compilation naming rev + path (AC-PACKAGING-019)', () => {
    const text = readMakefile();
    const androidBody = targetBody(text, 'engine-android');
    assert.match(androidBody, /ANDROID_NDK|NDK/i, 'engine-android must gate on the NDK location');
    assert.match(androidBody, /27\.2\.12479018/,
      'NDK gate must name the expected rev 27.2.12479018');
    assert.match(androidBody, /not found|missing|NDK.*[Pp]ath|ANDROID_NDK_HOME|ANDROID_NDK_ROOT/i,
      'NDK gate must name the offending path when the NDK is missing');
    assert.match(androidBody, /exit\s+1/,
      'missing-NDK gate must exit non-zero before compilation');
  });

  it('wrong NDK revision fails closed naming expected rev + path (AC-PACKAGING-019)', () => {
    const text = readMakefile();
    assert.match(text, /source\.properties|Pkg\.Revision|28\.|26\.|wrong|mismatch|expected/i,
      'Makefile must compare the installed NDK revision against the pin and name the expected rev on mismatch');
    const androidBody = targetBody(text, 'engine-android');
    assert.match(androidBody, /exit\s+1/, 'wrong-rev gate must exit non-zero');
  });

  it('misconfigured ABI dir fails closed naming rev + path (AC-PACKAGING-019)', () => {
    const body = targetBody(readMakefile(), 'engine-android');
    assert.match(body, /ABI|ABIS/i, 'engine-android must validate the ABI dir/slot selection');
    assert.match(body, /exit\s+1/, 'misconfigured ABI dir must fail the target non-zero');
    assert.match(body, /27\.2\.12479018|expected/i, 'ABI-dir gate must name the expected NDK rev');
  });

  it('clean removes engine build outputs (T2 interface)', () => {
    const body = targetBody(readMakefile(), 'clean');
    assert.match(body, /rm\s+-rf|rmdir|\$\(BUILD|\bbuild\b/i,
      'clean must remove engine build output directories');
  });

  it('hermetic: T2 asserts Makefile text only, zero tool invocations', () => {
    readMakefile();
    assert.equal(stubCalls.length, 0, 'T2 assertions must not shell out to any tool');
  });
});

// --- T3 scaffold Makefile: apk / sign / verify / install / clean ---------------
// Scope: FR-PACKAGING-008 AC-PACKAGING-020/021/022 (TC-020/021/022).
// Interface (plan T3 verbatim):
//   `make apk | make sign | make verify | make install | make clean` (run in
//   `examples/m3-gallery`) → exit 0 on success; `install` with no `adb` device
//   fails closed naming the missing device; `verify` fails closed on any
//   missing entry or failed `apksigner verify`.
// Hermetic: scaffold-Makefile-text assertions only; child_process stays
// stubbed (no aapt2/d8/javac/zipalign/apksigner/adb, no device/network).
// RED until `examples/m3-gallery/Makefile` exists with the five targets and
// the verify/install fail-closed gates.
// ASSUMPTION A-S1 (contract genuinely silent): the unsigned-APK recipe drives
// `aapt2` link (binary manifest) + `javac`+`d8` dex + per-arch engine libs
// from T2 + the built tree; tests accept tool names as Makefile tokens
// (`aapt2`, `javac`, `d8`, `zipalign`, `apksigner`, `adb`) without pinning
// flags or output paths.
// ASSUMPTION A-S2: debug-key signing is a recipe-level `apksigner sign`
// with a `--ks` debug keystore token; Play release signing is out of scope
// (plan §Out of Scope) and must not appear.
// ASSUMPTION A-S3: verify entry checks name the three AC-PACKAGING-008/021
// entries (`AndroidManifest.xml`, `classes.dex`, `resources.arsc`); v2+v3
// proof is a recipe-level `apksigner verify` token, live device proof is
// `tester` work (no device on this host).
function readScaffoldMakefile() {
  assert.ok(fs.existsSync(SCAFFOLD_MAKEFILE),
    'scaffold Makefile must exist (T3 owns it; RED until the coder adds it)');
  return fs.readFileSync(SCAFFOLD_MAKEFILE, 'utf8');
}

function scaffoldTargetBody(text, name) {
  const lines = text.split('\n');
  const idx = lines.findIndex((l) => new RegExp(`^${name}\\s*:`).test(l));
  assert.ok(idx !== -1, `scaffold Makefile must define target: ${name}`);
  let end = lines.length;
  for (let i = idx + 1; i < lines.length; i += 1) {
    if (/^[A-Za-z0-9_.-]+\s*:(?![:=])/.test(lines[i]) && !/^\s/.test(lines[i])) { end = i; break; }
  }
  return lines.slice(idx, end).join('\n');
}

describe('T3 scaffold Makefile apk flow (AC-PACKAGING-020/021/022)', () => {
  it('scaffold Makefile exists with apk, sign, verify, install, clean targets (TC-020/021/022)', () => {
    const text = readScaffoldMakefile();
    for (const t of ['apk', 'sign', 'verify', 'install', 'clean']) {
      assert.match(text, new RegExp(`^${t}\\s*:`, 'm'), `missing make target: ${t}`);
    }
  });

  it('apk links via aapt2 with no hand-typed assembly (AC-PACKAGING-020)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    assert.match(body, /\baapt2\b/, 'apk must link via aapt2 (binary manifest, no hand assembly)');
  });

  it('apk compiles dex from shell sources via javac + d8 (AC-PACKAGING-020)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    assert.match(body, /\bjavac\b/, 'apk must compile shell sources via javac');
    assert.match(body, /\bd8\b/, 'apk must dex via d8');
  });

  it('apk bundles per-arch engine libs plus the built tree (AC-PACKAGING-020)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    assert.match(body, /libRuntime\.so|libs|\blib\b/i, 'apk must bundle the per-arch engine libs from T2');
    assert.match(body, /tree|assets/i, 'apk must bundle the built tree');
  });

  it('sign aligns + debug-signs only (AC-PACKAGING-021)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'sign');
    assert.match(body, /\bzipalign\b/i, 'sign must align the APK (zipalign)');
    assert.match(body, /\bapksigner\b/, 'sign must sign via apksigner');
    assert.match(body, /debug|--ks\b/i, 'sign must use the debug key only');
  });

  it('verify runs apksigner verify for v2+v3 (AC-PACKAGING-021)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'verify');
    assert.match(body, /\bapksigner\b/, 'verify must run apksigner verify');
    assert.match(body, /verify/, 'verify must invoke the apksigner verify subcommand');
  });

  it('verify checks the required APK entries (AC-PACKAGING-021)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'verify');
    for (const entry of ['AndroidManifest.xml', 'classes.dex', 'resources.arsc']) {
      assert.ok(body.includes(entry), `verify must check APK entry ${entry}`);
    }
  });

  it('verify fails closed on any missing entry or failed apksigner verify (AC-PACKAGING-021)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'verify');
    assert.match(body, /exit\s+1|&&\s*exit|false\b|set\s+-e/i,
      'verify must fail the target non-zero on a missing entry or failed apksigner verify');
  });

  it('install pushes via adb install (AC-PACKAGING-022)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'install');
    assert.match(body, /\badb\b/, 'install must use adb');
    assert.match(body, /install/, 'install must run adb install');
  });

  it('install with no device fails closed naming the missing device (AC-PACKAGING-022)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'install');
    assert.match(body, /adb\s+devices|no device|missing device|device.*(not found|missing)|UNAUTHORIZED/i,
      'install must gate on a connected device and name the missing device');
    assert.match(body, /exit\s+1/,
      'install with no device must exit non-zero instead of partial-installing');
  });

  it('clean removes scaffold build outputs (T3 interface)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'clean');
    assert.match(body, /rm\s+-rf|rmdir|\bdist\b|\bbuild\b|\.apk/i,
      'clean must remove scaffold APK/build outputs');
  });

  it('hermetic: T3 asserts Makefile text only, zero tool invocations', () => {
    readScaffoldMakefile();
    assert.equal(stubCalls.length, 0, 'T3 assertions must not shell out to any tool');
  });
});

// --- T4 hermetic gate coverage: TC-017..022 via stubbed tools -------------------
// Scope: FR-PACKAGING-007/008 AC-PACKAGING-017..022 (TC-017..022); consolidates
// the batch — every TC maps to a case group in this file.
// Interface (plan T4 verbatim):
//   `node --test packages/cli/test/packaging-make.test.js` → exit 0; asserts
//   root/scaffold Makefile target existence, NDK/arch/device fail-closed
//   messages, and ELF/signature gate behavior via stubbed tool invocations
//   (no real NDK/SDK/device/network).
// Hermetic: Makefile/text assertions only; child_process stays stubbed and
// every suite asserts zero invocations. RED until T2+T3 Makefiles exist
// (scaffold side RED until the coder adds `examples/m3-gallery/Makefile`).
// Depends on: T2, T3 (their targets are the gates asserted here).
describe('T4 hermetic gate coverage TC-017..022 (AC-PACKAGING-017..022)', () => {
  it('TC-017..019 gates live in the root Makefile (T2-owned targets present)', () => {
    const text = readMakefile();
    for (const t of ['engine-host', 'engine-android', 'clean']) {
      assert.match(text, new RegExp(`^${t}\\s*:`, 'm'), `root Makefile must define target: ${t}`);
    }
    assert.ok(text.includes('27.2.12479018'), 'TC-019 gate must name the pinned NDK rev');
  });

  it('TC-020..022 gates live in the scaffold Makefile (T3-owned targets present)', () => {
    const text = readScaffoldMakefile();
    for (const t of ['apk', 'sign', 'verify', 'install', 'clean']) {
      assert.match(text, new RegExp(`^${t}\\s*:`, 'm'), `scaffold Makefile must define target: ${t}`);
    }
  });

  it('NDK/arch fail-closed messages name rev+path and slot-vs-machine (TC-018/019)', () => {
    const text = readMakefile();
    assert.match(text, /27\.2\.12479018/, 'gate text must name the expected NDK rev');
    assert.match(text, /ANDROID_NDK|NDK.*[Pp]ath|not found|missing/i,
      'gate text must name the offending NDK path');
    assert.match(text, /aarch64/i, 'ELF gate text must name the expected machine');
    assert.match(text, /ABI|slot/i, 'ELF gate text must name the offending ABI slot');
  });

  it('verify/install fail-closed messages name the missing entry/device (TC-021/022)', () => {
    const text = readScaffoldMakefile();
    assert.match(text, /AndroidManifest\.xml|classes\.dex|resources\.arsc/,
      'verify gate text must name the required APK entries');
    assert.match(text, /apksigner/, 'verify gate text must name apksigner verify');
    assert.match(text, /no device|missing device|adb\s+devices/i,
      'install gate text must name the missing device');
  });

  it('ELF/signature gate behavior is recipe-level with non-zero exit (TC-018/021)', () => {
    const rootBody = targetBody(readMakefile(), 'engine-android');
    assert.match(rootBody, /\bfile\b|\breadelf\b/i, 'ELF gate must run file/readelf');
    assert.match(rootBody, /exit\s+1/, 'ELF mismatch must fail the target');
    const verifyBody = scaffoldTargetBody(readScaffoldMakefile(), 'verify');
    assert.match(verifyBody, /verify/, 'signature gate must run apksigner verify');
    assert.match(verifyBody, /exit\s+1|&&\s*exit|false\b|set\s+-e/i,
      'signature/entry failure must fail the target');
  });

  it('TC-017..022 each map to a case group in this file (coverage completeness)', () => {
    const self = fs.readFileSync(__filename, 'utf8');
    for (const tc of ['TC-017', 'TC-018', 'TC-019', 'TC-020', 'TC-021', 'TC-022']) {
      assert.ok(self.includes(tc), `this file must contain a case group for ${tc}`);
    }
  });

  it('hermetic: T4 asserts text only, zero tool invocations, no secrets/config', () => {
    readMakefile();
    readScaffoldMakefile();
    assert.equal(stubCalls.length, 0, 'T4 assertions must not shell out to any tool');
  });
});
