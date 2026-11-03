'use strict';
// T9 — Generic engine state-carriage setter contract (plan Batch 4 / T9).
// Hermetic: node:test + node:assert/strict only. Static source pins over
// `engine/runtime/src/jni_bridge.cpp` plus a hermetic truth-table model of
// the specified generic setter semantics (fail-closed, stores only opaque
// strings, never routes, never touches renderer/layout). No device, no
// downloads, no secrets, no config, no host-env mutation. No production
// code touched.
//
// Specified behavior (plan T9 Interface):
//   `nativeSetShellState(shellRoute: String, shellNotice: String): void`
//   opaque store (jint 1 applied / 0 denied); validity by route table only.
// Supersedes the retired `nativeSetDemoShellState(handle, tab, deniedLink)`
// seam: no demo-tab allowlist, no demo comparisons in the TU.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const RUN_ROOT = join(__dirname, '..', '..', '..');
const JNI_PATH = join(RUN_ROOT, 'engine', 'runtime', 'src', 'jni_bridge.cpp');

function read(p) {
  return readFileSync(p, 'utf8');
}

// Hermetic model of the specified generic setter truth table. Stores only
// opaque strings; any non-empty route (≤2048) applies — validity is decided
// by the route table in JS, never here. Null notice normalizes to "".
// Bad handle / null-empty-overlong route deny with 0, state unchanged.
const MAX_CHARS = 2048;
function modelSetShellState(store, handle, shellRoute, shellNotice) {
  if (!store.has(handle)) return { code: 0, applied: false };
  if (typeof shellRoute !== 'string' || shellRoute.length === 0 || shellRoute.length > MAX_CHARS) {
    return { code: 0, applied: false };
  }
  if (shellNotice !== undefined && shellNotice !== null && typeof shellNotice !== 'string') {
    return { code: 0, applied: false };
  }
  if (typeof shellNotice === 'string' && shellNotice.length > MAX_CHARS) {
    return { code: 0, applied: false };
  }
  store.set(handle, { shellRoute, shellNotice: shellNotice == null ? '' : shellNotice });
  return { code: 1, applied: true };
}

describe('T9 — nativeSetShellState entry exists and is generic', () => {
  it('jni_bridge.cpp exports Java_com_muse_host_MainActivity_nativeSetShellState', () => {
    const src = read(JNI_PATH);
    assert.match(
      src,
      /Java_com_muse_host_MainActivity_nativeSetShellState/,
      'jni_bridge.cpp must export the nativeSetShellState JNI entry',
    );
  });

  it('setter entry returns jint and fail-closes (return-0 paths, applied-1 path)', () => {
    const src = read(JNI_PATH);
    const idx = src.indexOf('Java_com_muse_host_MainActivity_nativeSetShellState');
    assert.ok(idx >= 0, 'entry must exist (companion pin)');
    const window = src.slice(idx, idx + 2600);
    assert.match(window, /\bjint\b/, 'setter entry must return jint (1 applied / 0 denied)');
    assert.match(window, /return 0/, 'setter entry must have a fail-closed return-0 path');
    assert.match(window, /return static_cast<jint>\(1\)/, 'setter entry must have an applied-1 path');
  });

  it('seam carries no demo identifiers (nativeSetDemoShellState/demoTab/demoDeniedLink absent)', () => {
    const src = read(JNI_PATH);
    assert.ok(!src.includes('nativeSetDemoShellState'), 'demo-named JNI entry must be gone');
    assert.ok(!src.includes('demoTab'), 'demoTab field must be gone');
    assert.ok(!src.includes('demoDeniedLink'), 'demoDeniedLink field must be gone');
  });
});

describe('T9 — setter truth table: opaque store applies (validity by route table only)', () => {
  it('non-empty opaque route applies with code 1', () => {
    const store = new Map([[1, { shellRoute: 'lens', shellNotice: '' }]]);
    const out = modelSetShellState(store, 1, 'lens', '');
    assert.equal(out.code, 1);
    assert.equal(store.get(1).shellRoute, 'lens');
  });

  it('optional shellNotice string stores verbatim', () => {
    const store = new Map([[7, { shellRoute: 'lens', shellNotice: '' }]]);
    const out = modelSetShellState(store, 7, 'field', 'muse-demo://tab/nope');
    assert.equal(out.code, 1);
    assert.deepEqual(store.get(7), { shellRoute: 'field', shellNotice: 'muse-demo://tab/nope' });
  });

  it('null shellNotice normalizes to empty (still applied)', () => {
    const store = new Map([[7, { shellRoute: 'lens', shellNotice: 'x' }]]);
    const out = modelSetShellState(store, 7, 'list', null);
    assert.equal(out.code, 1);
    assert.equal(store.get(7).shellNotice, '');
  });
});

describe('T9 — setter truth table: fail-closed cases return 0 with state unchanged', () => {
  it('null/empty/overlong route denies with code 0 and keeps prior state', () => {
    for (const bad of [null, undefined, '', 'x'.repeat(MAX_CHARS + 1), 42, {}, []]) {
      const store = new Map([[1, { shellRoute: 'lens', shellNotice: '' }]]);
      const out = modelSetShellState(store, 1, bad, '');
      assert.equal(out.code, 0, `route ${String(bad).slice(0, 20)} must deny`);
      assert.deepEqual(store.get(1), { shellRoute: 'lens', shellNotice: '' });
    }
  });

  it('bad handle denies with code 0', () => {
    for (const badHandle of [0, -1, 9999, null]) {
      const store = new Map([[1, { shellRoute: 'lens', shellNotice: '' }]]);
      const out = modelSetShellState(store, badHandle, 'field', '');
      assert.equal(out.code, 0);
      assert.deepEqual(store.get(1), { shellRoute: 'lens', shellNotice: '' });
    }
  });

  it('non-string or overlong shellNotice denies with code 0 and keeps prior state', () => {
    const store = new Map([[1, { shellRoute: 'lens', shellNotice: '' }]]);
    assert.equal(modelSetShellState(store, 1, 'field', 42).code, 0);
    assert.equal(modelSetShellState(store, 1, 'field', 'x'.repeat(MAX_CHARS + 1)).code, 0);
    assert.deepEqual(store.get(1), { shellRoute: 'lens', shellNotice: '' });
  });

  it('setter makes no routing decision: unknown opaque route still stores (route table decides)', () => {
    const store = new Map([[1, { shellRoute: 'lens', shellNotice: '' }]]);
    const out = modelSetShellState(store, 1, 'gallery-story-x', '');
    assert.equal(out.code, 1);
    assert.equal(store.get(1).shellRoute, 'gallery-story-x');
  });
});
