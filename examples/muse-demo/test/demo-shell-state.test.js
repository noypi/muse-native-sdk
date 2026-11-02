'use strict';
// T6 — Engine state-carriage setter contract (plan Batch 4).
// Hermetic: node:test + node:assert/strict only. Static source pins over
// `engine/runtime/src/jni_bridge.cpp` + `examples/muse-demo/src/MainActivity.java`
// plus a hermetic truth-table model of the specified setter semantics
// (fail-closed, stores only validated strings, never routes, never touches
// renderer/layout). No device, no downloads, no secrets, no config,
// no host-env mutation. No production code touched.
//
// Specified behavior (plan T6 Interface):
//   `nativeSetDemoShellState(handle: long, tab: String, deniedLink: String): int`
//   returns `1` applied, `0` denied/fail-closed (null/empty/unknown tab,
//   bad handle); stores only validated strings, makes no routing decision,
//   never alters renderer/layout code.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const RUN_ROOT = join(__dirname, '..', '..', '..');
const JNI_PATH = join(RUN_ROOT, 'engine', 'runtime', 'src', 'jni_bridge.cpp');
const ACTIVITY_PATH = join(RUN_ROOT, 'examples', 'muse-demo', 'src', 'MainActivity.java');

function read(p) {
  return readFileSync(p, 'utf8');
}

// Hermetic model of the specified setter truth table. This mirrors ONLY the
// plan's Interface line (allowlist lens|field|list, 1 applied / 0 denied,
// fail-closed with state unchanged). The coder's real JNI entry must satisfy
// the same table; this model is the red-state oracle until it lands.
const DEMO_TABS = ['lens', 'field', 'list'];
function modelSetDemoShellState(store, handle, tab, deniedLink) {
  if (!store.has(handle)) return { code: 0, applied: false };
  if (typeof tab !== 'string' || !DEMO_TABS.includes(tab)) return { code: 0, applied: false };
  if (deniedLink !== undefined && deniedLink !== null && typeof deniedLink !== 'string') {
    return { code: 0, applied: false };
  }
  store.set(handle, { tab, deniedLink: deniedLink == null ? '' : deniedLink });
  return { code: 1, applied: true };
}

describe('T6 — nativeSetDemoShellState entry exists on both sides of the seam', () => {
  it('MainActivity.java declares nativeSetDemoShellState(long, String, String): int', () => {
    const src = read(ACTIVITY_PATH);
    assert.match(
      src,
      /nativeSetDemoShellState\s*\(\s*long\b[^)]*String\b[^)]*String\b[^)]*\)/,
      'MainActivity.java must declare nativeSetDemoShellState(handle: long, tab: String, deniedLink: String): int',
    );
  });

  it('jni_bridge.cpp exports Java_com_muse_host_MainActivity_nativeSetDemoShellState', () => {
    const src = read(JNI_PATH);
    assert.match(
      src,
      /Java_com_muse_host_MainActivity_nativeSetDemoShellState/,
      'jni_bridge.cpp must export the nativeSetDemoShellState JNI entry',
    );
  });

  it('setter entry returns int and fail-closes on bad handle (source pin)', () => {
    const src = read(JNI_PATH);
    const idx = src.indexOf('Java_com_muse_host_MainActivity_nativeSetDemoShellState');
    assert.ok(idx >= 0, 'entry must exist (companion pin)');
    const window = src.slice(idx, idx + 2000);
    assert.match(window, /\bjint\b/, 'setter entry must return jint (1 applied / 0 denied)');
    assert.match(window, /return 0/, 'setter entry must have a fail-closed return-0 path');
  });
});

describe('T6 — setter truth table: valid lens|field|list (+ optional denied-link) applies', () => {
  for (const tab of DEMO_TABS) {
    it(`valid tab '${tab}' applies with code 1`, () => {
      const store = new Map([[1, { tab: 'lens', deniedLink: '' }]]);
      const out = modelSetDemoShellState(store, 1, tab, '');
      assert.equal(out.code, 1);
      assert.equal(store.get(1).tab, tab);
    });
  }

  it('valid tab with denied-link string stores both strings', () => {
    const store = new Map([[7, { tab: 'lens', deniedLink: '' }]]);
    const out = modelSetDemoShellState(store, 7, 'field', 'muse-demo://tab/nope');
    assert.equal(out.code, 1);
    assert.deepEqual(store.get(7), { tab: 'field', deniedLink: 'muse-demo://tab/nope' });
  });

  it('null denied-link normalizes to empty (still applied)', () => {
    const store = new Map([[7, { tab: 'lens', deniedLink: 'x' }]]);
    const out = modelSetDemoShellState(store, 7, 'list', null);
    assert.equal(out.code, 1);
    assert.equal(store.get(7).deniedLink, '');
  });
});

describe('T6 — setter truth table: fail-closed cases return 0 with state unchanged', () => {
  const BAD_TABS = [null, undefined, '', 'LENS', 'nope', 'lens/extra', 42, {}, []];

  for (const bad of BAD_TABS) {
    it(`bad tab ${String(bad)} denies with code 0 and keeps prior state`, () => {
      const store = new Map([[1, { tab: 'lens', deniedLink: '' }]]);
      const out = modelSetDemoShellState(store, 1, bad, '');
      assert.equal(out.code, 0);
      assert.deepEqual(store.get(1), { tab: 'lens', deniedLink: '' });
    });
  }

  for (const badHandle of [0, -1, 9999, null]) {
    it(`bad handle ${String(badHandle)} denies with code 0`, () => {
      const store = new Map([[1, { tab: 'lens', deniedLink: '' }]]);
      const out = modelSetDemoShellState(store, badHandle, 'field', '');
      assert.equal(out.code, 0);
      assert.deepEqual(store.get(1), { tab: 'lens', deniedLink: '' });
    });
  }

  it('non-string denied-link denies with code 0 and keeps prior state', () => {
    const store = new Map([[1, { tab: 'lens', deniedLink: '' }]]);
    const out = modelSetDemoShellState(store, 1, 'field', 42);
    assert.equal(out.code, 0);
    assert.deepEqual(store.get(1), { tab: 'lens', deniedLink: '' });
  });

  it('setter makes no routing decision: unknown tab never invents a page', () => {
    const store = new Map([[1, { tab: 'lens', deniedLink: '' }]]);
    const out = modelSetDemoShellState(store, 1, 'gallery-story-x', '');
    assert.equal(out.code, 0);
    assert.equal(store.get(1).tab, 'lens');
  });
});
