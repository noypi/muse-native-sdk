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

// --- Review fix: exact x86 slot token + NDK_HOME re-derived (T2) ---------
// Code-review follow-up (BLOCKER + apk MAJORS fixed in Makefiles): tighten the
// T2 gates so (a) the cmake configure line cannot reference an unset per-line
// shell var, and (d) the x86 allow-list slot cannot be subsumed by x86_64.
// Hermetic: Makefile-text assertions only; child_process stays stubbed.
describe('T2 review tightening: NDK_HOME + exact x86 slot (AC-PACKAGING-018/019)', () => {
  it('cmake configure re-derives NDK_HOME on the same recipe line (no unset per-line var)', () => {
    const body = targetBody(readMakefile(), 'engine-android');
    const lines = body.split('\n');
    const configureLines = lines.filter((l) => l.includes('CMAKE_TOOLCHAIN_FILE'));
    assert.ok(configureLines.length >= 1, 'engine-android must configure via a CMAKE_TOOLCHAIN_FILE line');
    for (const line of configureLines) {
      assert.ok(line.includes('$$NDK_HOME'),
        'configure line must reference the derived $$NDK_HOME toolchain path');
      assert.ok(line.includes('NDK_HOME='),
        'configure line must re-derive NDK_HOME on the same shell line (each make recipe line is a fresh shell; a bare $$NDK_HOME from another line is unset)');
      assert.ok(line.indexOf('NDK_HOME=') < line.indexOf('$$NDK_HOME'),
        'NDK_HOME= assignment must precede the $$NDK_HOME use on the configure line');
    }
  });

  it('x86 slot match is an exact token so x86_64 does not subsume x86 (AC-PACKAGING-018)', () => {
    const text = readMakefile();
    // Padded-token match: " $$ABI " against " x86 " / " x86_64 " alternatives.
    assert.ok(text.includes('" x86 "') || text.includes("' x86 '") || text.includes(' x86 "'),
      'allow-list gate must match x86 as an exact space-delimited slot token');
    assert.ok(text.includes('x86_64'), 'allow-list gate must still list the x86_64 slot distinctly');
    assert.ok(!text.includes('*x86*)'),
      'allow-list gate must not use a bare *x86* pattern (it would subsume x86_64 into x86)');
  });

  it('hermetic: review-tightening T2 asserts text only, zero tool invocations', () => {
    readMakefile();
    assert.equal(stubCalls.length, 0, 'review T2 assertions must not shell out to any tool');
  });
});

// --- Review fix: apk link-vs-zip paths + gated per-ABI lib copies (T3) --------
// Code-review follow-up (apk MAJORS fixed in scaffold Makefile): (b) the aapt2
// link output must differ from the final zip path with manifest/resources
// staged in between; (c) lib-copy lines must not hide a missing engine lib
// behind `|| true` — every ABI needs a lib/<abi>/ copy plus an explicit
// existence gate. Hermetic: scaffold-Makefile-text assertions only.
describe('T3 review tightening: link/zip paths + per-ABI lib gates (AC-PACKAGING-020)', () => {
  it('aapt2 link output path differs from the final zip path', () => {
    const text = readScaffoldMakefile();
    const body = scaffoldTargetBody(text, 'apk');
    const linkMatch = /aapt2\s+link[^;\n]*-o\s+(\S+)/.exec(body);
    assert.ok(linkMatch, 'apk must pass -o <linked-zip> to aapt2 link');
    assert.ok(/UNSIGNED_APK\s*[:?]?=/.test(text), 'scaffold Makefile must define UNSIGNED_APK');
    assert.ok(/LINKED_ZIP\s*[:?]?=/.test(text), 'scaffold Makefile must define LINKED_ZIP');
    const linkedDecl = /LINKED_ZIP\s*[:?]?=\s*(\S+)/.exec(text)[1];
    const unsignedDecl = /UNSIGNED_APK\s*[:?]?=\s*(\S+)/.exec(text)[1];
    assert.notEqual(linkedDecl, unsignedDecl,
      `aapt2 link output (${linkedDecl}) must differ from the final zip (${unsignedDecl})`);
    assert.ok(body.includes('$(LINKED_ZIP)'), 'apk recipe must link to $(LINKED_ZIP)');
    assert.ok(body.includes('$(UNSIGNED_APK)') || body.includes('$(notdir $(UNSIGNED_APK))'),
      'final zip step must target $(UNSIGNED_APK)');
  });

  it('manifest/resources are staged from the linked zip before the final zip', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    const stageIdx = body.indexOf('$(LINKED_ZIP)');
    const zipIdx = body.indexOf('zip -r');
    assert.ok(stageIdx !== -1 && zipIdx !== -1, 'apk must stage from $(LINKED_ZIP) then run zip -r');
    assert.ok(stageIdx < zipIdx, 'manifest/resources staging must precede the final zip');
    const staging = body.slice(0, zipIdx);
    assert.ok(staging.includes('AndroidManifest.xml'), 'staging must include AndroidManifest.xml');
    assert.ok(staging.includes('resources.arsc'), 'staging must include resources.arsc');
  });

  it('lib-copy lines never use bare || true (missing engine lib must fail)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    const copyLines = body.split('\n').filter((l) => /^\s*cp\b/.test(l) && l.includes('libRuntime.so'));
    assert.ok(copyLines.length >= 4, `expected per-ABI lib copies, found ${copyLines.length}`);
    for (const line of copyLines) {
      assert.ok(!line.includes('|| true'),
        `lib-copy line must not swallow a missing lib with || true: ${line.trim()}`);
    }
  });

  it('every ABI has a lib/<abi>/ copy with an explicit existence gate', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    for (const abi of ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86']) {
      assert.ok(body.includes(`lib/${abi}/`),
        `apk must copy the engine lib into lib/${abi}/ for slot ${abi}`);
      const gateRe = new RegExp(`test\\s+-s\\s+[^\\n]*${abi.replace('+', '\\+')}[^\\n]*libRuntime\\.so`);
      assert.match(body, gateRe,
        `apk must gate slot ${abi} with an explicit test -s existence check before copying`);
    }
  });

  it('hermetic: review-tightening T3 asserts text only, zero tool invocations', () => {
    readScaffoldMakefile();
    assert.equal(stubCalls.length, 0, 'review T3 assertions must not shell out to any tool');
  });
});

// --- BUG-PACKAGING-002 T1: resources.arsc Stored + 4-byte aligned --------------
// Scope: FR-PACKAGING-004 (no ACs per bug entry; AC-PACKAGING-008/009 adjacent
// context only). Interface (plan T1 verbatim):
//   `node --test packages/cli/test/packaging-make.test.js → exit 0` when
//   (a) scaffold-Makefile text contains stale-archive hygiene (`rm -f` of
//   `$(UNSIGNED_APK)` before reassembly) + an explicit Stored arsc step
//   (`zip -g -0` naming `resources.arsc`, arsc excluded from the deflated
//   `zip -r` set), (b) `verify`-target text contains a fail-closed
//   Stored+alignment gate naming `resources.arsc` (non-zero exit otherwise),
//   (c) a fixture-APK central-directory parser asserts `resources.arsc`
//   method 0 (Stored) with data-offset 4-byte aligned and fails a Deflated
//   fixture; hermetic (stubbed child_process, stdlib only, zero tool
//   invocations).
// Hermetic: scaffold-Makefile-text assertions + pure-JS central-directory
// reader over two minimal in-test fixture zips (no `unzip` binary, no
// aapt2/zipalign/apksigner/adb, no device/network). Must RED on the current
// recipe (no `-g`, no `rm -f`, no verify gate). Negative control: Deflated
// fixture must fail the gate.
// ASSUMPTION A-B1 (contract genuinely silent): 4-byte alignment is checked
// as `dataOffset % 4 === 0` where dataOffset is the local-file data start
// derived from the central directory + local header (zipalign 4-byte
// alignment proxy); extra-field padding details are coder-owned.
function arscZipLines(body) {
  return body.split('\n').filter((l) => /(^|\s)zip\b/.test(l));
}

function zipRArscSet(body) {
  // Returns the file-set tokens of the deflated `zip -r` pass (tokens after
  // the archive operand), or null when no `zip -r` line exists.
  for (const line of arscZipLines(body)) {
    for (const seg of line.split('&&')) {
      const m = /zip\s+(?:-[^\s]*r[^\s]*)\s+\S+\s+(.*)/.exec(seg);
      if (m) return m[1];
    }
  }
  return null;
}

// Minimal in-test ZIP builder: local headers + central directory + EOCD,
// stdlib only (no compression library needed — Deflated fixture stores
// pre-deflated bytes verbatim with method 8).
function buildFixtureZip(entries) {
  const chunks = [];
  const centrals = [];
  let offset = 0;
  for (const e of entries) {
    const name = Buffer.from(e.name, 'utf8');
    const data = Buffer.from(e.data);
    const lh = Buffer.alloc(30);
    lh.writeUInt32LE(0x04034b50, 0);
    lh.writeUInt16LE(20, 4);
    lh.writeUInt16LE(0, 6);
    lh.writeUInt16LE(e.method, 8);
    lh.writeUInt16LE(0, 10);
    lh.writeUInt16LE(0, 12);
    lh.writeUInt32LE(0, 14);
    lh.writeUInt32LE(data.length, 18);
    lh.writeUInt32LE(data.length, 22);
    lh.writeUInt16LE(name.length, 26);
    lh.writeUInt16LE(0, 28);
    chunks.push(lh, name, data);
    const cd = Buffer.alloc(46);
    cd.writeUInt32LE(0x02014b50, 0);
    cd.writeUInt16LE(20, 4);
    cd.writeUInt16LE(20, 6);
    cd.writeUInt16LE(0, 8);
    cd.writeUInt16LE(e.method, 10);
    cd.writeUInt16LE(0, 12);
    cd.writeUInt16LE(0, 14);
    cd.writeUInt32LE(0, 16);
    cd.writeUInt32LE(data.length, 20);
    cd.writeUInt32LE(data.length, 24);
    cd.writeUInt16LE(name.length, 28);
    cd.writeUInt16LE(0, 30);
    cd.writeUInt16LE(0, 32);
    cd.writeUInt16LE(0, 34);
    cd.writeUInt16LE(0, 36);
    cd.writeUInt32LE(0, 38);
    cd.writeUInt32LE(offset, 42);
    centrals.push(cd, name);
    offset += lh.length + name.length + data.length;
  }
  const cdStart = offset;
  const cdBytes = Buffer.concat(centrals);
  chunks.push(cdBytes);
  offset += cdBytes.length;
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(0, 8);
  eocd.writeUInt16LE(0, 10);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(cdBytes.length, 12);
  eocd.writeUInt32LE(cdStart, 16);
  chunks.push(eocd);
  return Buffer.concat(chunks);
}

// Pure-JS central-directory reader: returns { method, dataOffset } for the
// named entry. Throws when the entry or EOCD is absent.
function readCentralEntry(buf, entryName) {
  const b = Buffer.from(buf);
  let eocd = -1;
  for (let i = b.length - 22; i >= 0; i -= 1) {
    if (b.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  }
  assert.ok(eocd !== -1, 'fixture zip must contain EOCD');
  const count = b.readUInt16LE(eocd + 10);
  let p = b.readUInt32LE(eocd + 16);
  for (let n = 0; n < count; n += 1) {
    assert.equal(b.readUInt32LE(p), 0x02014b50, 'central directory entry signature');
    const method = b.readUInt16LE(p + 10);
    const nameLen = b.readUInt16LE(p + 28);
    const extraLen = b.readUInt16LE(p + 30);
    const commentLen = b.readUInt16LE(p + 32);
    const lhOff = b.readUInt32LE(p + 42);
    const name = b.subarray(p + 46, p + 46 + nameLen).toString('utf8');
    if (name === entryName) {
      assert.equal(b.readUInt32LE(lhOff), 0x04034b50, 'local header signature');
      const lhName = b.readUInt16LE(lhOff + 26);
      const lhExtra = b.readUInt16LE(lhOff + 28);
      return { method, dataOffset: lhOff + 30 + lhName + lhExtra };
    }
    p += 46 + nameLen + extraLen + commentLen;
  }
  throw new Error(`fixture zip missing entry: ${entryName}`);
}

function arscGatePass(zipBuf) {
  const e = readCentralEntry(zipBuf, 'resources.arsc');
  return e.method === 0 && e.dataOffset % 4 === 0;
}

describe('BUG-PACKAGING-002 T1 arsc Stored + aligned (FR-PACKAGING-004)', () => {
  it('apk removes the stale archive before reassembly (rm -f $(UNSIGNED_APK))', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    const rmIdx = body.indexOf('rm -f');
    const zipIdx = body.indexOf('zip -r');
    assert.ok(rmIdx !== -1, 'apk must rm -f any stale archive before the final zip');
    assert.ok(body.includes('$(UNSIGNED_APK)'),
      'stale-archive hygiene must name $(UNSIGNED_APK)');
    assert.ok(zipIdx !== -1 && rmIdx < zipIdx,
      'rm -f hygiene must precede the final zip reassembly');
  });

  it('apk keeps resources.arsc out of the deflated zip -r set (detail)', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    const set = zipRArscSet(body);
    assert.ok(set !== null, 'apk must assemble the deflatable set via zip -r');
    assert.ok(!set.includes('resources.arsc'),
      'deflated zip -r set must exclude resources.arsc (Stored step owns it)');
  });

  it('apk adds resources.arsc via an explicit zip -g -0 Stored step', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'apk');
    const lines = arscZipLines(body).filter((l) => l.includes('resources.arsc'));
    assert.ok(lines.length >= 1, 'apk must add resources.arsc via an explicit zip step');
    const stored = lines.some((l) => /zip\s+[^\n]*-g[^\n]*-0[^\n]*resources\.arsc|zip\s+[^\n]*-0[^\n]*-g[^\n]*resources\.arsc/.test(l));
    assert.ok(stored,
      'resources.arsc must be added Stored via zip -g -0 (explicit grow flag so the step cannot silently no-op)');
  });

  it('verify fails closed on non-Stored or misaligned resources.arsc', () => {
    const body = scaffoldTargetBody(readScaffoldMakefile(), 'verify');
    assert.ok(body.includes('resources.arsc'), 'verify gate must name resources.arsc');
    assert.match(body, /Stored|method 0|zipalign -c/i,
      'verify gate must assert the Stored method / alignment state');
    assert.match(body, /exit\s+1/,
      'verify gate must exit non-zero when resources.arsc is not Stored+aligned');
  });

  it('central-directory reader: Stored aligned arsc passes the gate', () => {
    // "resources.arsc" (14 bytes) as the first entry → local data offset
    // 30+14=44, 44 % 4 === 0 (zipalign 4-byte proxy).
    const zip = buildFixtureZip([
      { name: 'resources.arsc', method: 0, data: [9, 9, 9, 9] },
      { name: 'AndroidManifest.xml', method: 8, data: [1, 2, 3, 4] },
    ]);
    const e = readCentralEntry(zip, 'resources.arsc');
    assert.equal(e.method, 0, 'fixture arsc must be Stored (method 0)');
    assert.equal(e.dataOffset % 4, 0, `fixture arsc data offset ${e.dataOffset} must be 4-byte aligned`);
    assert.equal(arscGatePass(zip), true, 'Stored+aligned arsc must pass the gate');
  });

  it('central-directory reader: Deflated arsc fails the gate (negative control)', () => {
    const zip = buildFixtureZip([
      { name: 'AndroidManifest.xml', method: 8, data: [1, 2, 3, 4] },
      { name: 'resources.arsc', method: 8, data: [7, 7, 7, 7] },
    ]);
    const e = readCentralEntry(zip, 'resources.arsc');
    assert.equal(e.method, 8, 'negative-control fixture arsc must be Deflated (method 8)');
    assert.equal(arscGatePass(zip), false, 'Deflated arsc must fail the Stored+aligned gate');
  });

  it('hermetic: T1 asserts Makefile text + in-memory zips only, zero tool invocations', () => {
    readScaffoldMakefile();
    assert.equal(stubCalls.length, 0, 'T1 assertions must not shell out to any tool');
  });
});
