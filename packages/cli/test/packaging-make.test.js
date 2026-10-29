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
