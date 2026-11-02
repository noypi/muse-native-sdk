'use strict';
// T8 — host security pins for demo-muse-demo-proof (security-review.md, 2026-11-02).
// Five requested controls over `examples/muse-demo/src/MainActivity.java`.
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. No adb,
// no device, no secrets, no config, no downloads. Java behavior is mirrored
// in-test as pure functions; every behavioral case pairs the mirror with a
// static source pin on MainActivity.java (gallery security-test precedent).
// No production code written here.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const MAIN_ACTIVITY = join(__dirname, '..', 'src', 'MainActivity.java');

function readActivity() {
  return readFileSync(MAIN_ACTIVITY, 'utf8');
}

function touchListenerBlock() {
  const text = readActivity();
  const start = text.indexOf('setOnTouchListener');
  assert.notEqual(start, -1, 'MainActivity.java must still own the touch listener');
  return text.slice(start, start + 8000);
}

// ---- Mirrors of the Java logic (kept in sync with MainActivity.java) ----
function shouldDispatchTouch(engineHandle) {
  return engineHandle !== 0;
}

function touchMissOrForward(x, y) {
  if (!Number.isFinite(x) || !Number.isFinite(y)) return -1;
  return 'forward';
}

// Minimal lifecycle model mirroring startRenderLoop/stopRenderLoop/onPause/onResume.
function makeLoopModel() {
  const model = {
    surfaceLive: false,
    paused: false,
    posts: 0,
    startRenderLoop() {
      if (model.paused || model.surfaceLive) return;
      model.surfaceLive = true;
      model.posts += 1;
    },
    stopRenderLoop() { model.surfaceLive = false; },
    onPause() { model.paused = true; model.surfaceLive = false; },
    doFrame() {
      if (!model.surfaceLive || model.paused) return false;
      return true; // would render
    },
  };
  return model;
}

describe('T8-1 — zero-handle touch gate (no nativeDispatchTouch when handle is 0)', () => {
  it('mirror: handle 0 never dispatches', () => {
    assert.equal(shouldDispatchTouch(0), false);
    assert.equal(shouldDispatchTouch(12345), true);
  });

  it('source pin: touch listener returns before any JNI call when engineHandle == 0', () => {
    const block = touchListenerBlock();
    const guard = block.search(/if\s*\(\s*engineHandle\s*==\s*0\s*\)/);
    assert.notEqual(guard, -1, 'zero-handle guard must exist in the touch listener');
    const jni = block.indexOf('nativeDispatchTouch');
    assert.notEqual(jni, -1, 'listener must still forward touches via nativeDispatchTouch');
    assert.ok(guard < jni, 'zero-handle guard must precede the JNI call');
  });
});

describe('T8-2 — NaN/extreme-coordinate contract (miss -1, never crash or OOB)', () => {
  it('mirror: NaN and ±Infinity resolve to miss (-1)', () => {
    for (const bad of [NaN, Infinity, -Infinity]) {
      assert.equal(touchMissOrForward(bad, 100), -1, `x=${String(bad)} must miss`);
      assert.equal(touchMissOrForward(100, bad), -1, `y=${String(bad)} must miss`);
    }
    assert.equal(touchMissOrForward(NaN, NaN), -1, 'NaN,NaN must miss');
  });

  it('mirror: finite coords (including off-screen megapixel values) forward without throwing', () => {
    assert.equal(touchMissOrForward(100, 200), 'forward');
    assert.equal(touchMissOrForward(1e9, -1e9), 'forward', 'off-screen extremes forward as finite floats');
  });

  it('source pin: listener finite-checks x/y before the JNI call', () => {
    const block = touchListenerBlock();
    assert.match(block, /Float\.isFinite\(x\)/, 'listener must finite-check x');
    assert.match(block, /Float\.isFinite\(y\)/, 'listener must finite-check y');
    const finitePos = block.search(/Float\.isFinite/);
    const jniPos = block.indexOf('nativeDispatchTouch');
    assert.ok(finitePos !== -1 && finitePos < jniPos, 'finite check must precede the JNI call');
  });
});

describe('T8-3 — double surface callback idempotence (single loop)', () => {
  it('mirror: repeated surfaceCreated/surfaceChanged posts the frame callback once', () => {
    const m = makeLoopModel();
    m.startRenderLoop();
    m.startRenderLoop();
    m.startRenderLoop();
    assert.equal(m.posts, 1, 'double/triple start must post exactly once');
    assert.equal(m.surfaceLive, true);
  });

  it('source pin: startRenderLoop guards on surfaceLive/paused; both callbacks route through it', () => {
    const text = readActivity();
    assert.match(text, /if\s*\(\s*paused\s*\|\|\s*surfaceLive\s*\)/, 'startRenderLoop must guard on paused||surfaceLive');
    const created = text.indexOf('surfaceCreated');
    const changed = text.indexOf('surfaceChanged');
    assert.notEqual(created, -1, 'surfaceCreated must exist');
    assert.notEqual(changed, -1, 'surfaceChanged must exist');
    assert.ok(text.slice(created, created + 2000).includes('startRenderLoop'), 'surfaceCreated must call startRenderLoop');
    assert.ok(text.slice(changed, changed + 2000).includes('startRenderLoop'), 'surfaceChanged must call startRenderLoop');
  });
});

describe('T8-4 — background stop (onPause stops frame posting)', () => {
  it('mirror: onPause stops posting; doFrame renders nothing while paused', () => {
    const m = makeLoopModel();
    m.startRenderLoop();
    assert.equal(m.doFrame(), true, 'live loop renders before pause');
    m.onPause();
    assert.equal(m.doFrame(), false, 'paused loop must not render');
  });

  it('source pin: onPause parks the loop; doFrame gates on paused; onResume restarts only if surface live', () => {
    const text = readActivity();
    const pauseAt = text.indexOf('void onPause');
    assert.notEqual(pauseAt, -1, 'onPause must exist');
    const pauseBlock = text.slice(pauseAt, pauseAt + 1200);
    assert.match(pauseBlock, /paused\s*=\s*true/, 'onPause must set paused=true');
    assert.match(pauseBlock, /removeFrameCallback/, 'onPause must remove the frame callback');
    assert.match(text, /if\s*\(\s*!surfaceLive\s*\|\|\s*paused\s*\)/, 'doFrame must early-return when !surfaceLive||paused');
    const resumeAt = text.indexOf('void onResume');
    assert.notEqual(resumeAt, -1, 'onResume must exist');
    const resumeBlock = text.slice(resumeAt, resumeAt + 1500);
    assert.match(resumeBlock, /if\s*\(\s*surfaceLive\s*\)/, 'onResume must restart only when the surface is still live');
  });
});

describe('T8-5 — first-frame log hygiene (no coords, handle, or asset bytes)', () => {
  it('source pin: first-frame Log.i is a static string with no interpolated values', () => {
    const text = readActivity();
    const logAt = text.indexOf('first-frame-present');
    assert.notEqual(logAt, -1, 'first-frame-present log must exist');
    const lineStart = text.lastIndexOf('\n', logAt);
    const lineEnd = text.indexOf('\n', logAt);
    const line = text.slice(lineStart, lineEnd);
    assert.match(line, /Log\.i\(TAG,/, 'first-frame log must use Log.i(TAG, ...)');
    assert.match(line, /"first-frame-present/, 'first-frame log must be a static string literal');
    assert.doesNotMatch(line, /\+/, 'first-frame log line must not concatenate values');
    assert.doesNotMatch(line, /engineHandle|getX|getY|treeBytes|x\s*,|y\s*,/, 'log line must not reference handle, coords, or asset bytes');
  });
});
