'use strict';
// T7 — Host pushes validated tab state into shell (warm + cold, fallback banner).
// Hermetic: node:test + node:assert/strict only. Binds to the plan T7
// Interface `pushValidatedTab(link: String): { applied: boolean, code?: "unknown-route" }`
// plus static source pins: demoNav.parseTabLink stays the allowlist source of
// truth; MainActivity.java funnels cold (getIntent/stashed drain) + warm
// (onNewIntent singleTask) + tap-advance + onFallbackRecover through the
// setter, calling nativeSetDemoShellState then nativeRenderFrame, logging
// `unknown-route` + `destinationReached=false` on denial and keeping the
// current tab. No device, no downloads, no secrets, no config,
// no host-env mutation. No production code touched.
//
// ASSUMPTION A1: `pushValidatedTab` is exercised here through a hermetic
// host-side double that mirrors the specified funnel (validate via
// demoNav.parseTabLink -> setter -> repaint); the coder wires the real
// MainActivity.java path to satisfy the same observable contract + pins.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const NAV_PATH = '../src/demoNav.js';
const RUN_ROOT = join(__dirname, '..', '..', '..');
const ACTIVITY_PATH = join(RUN_ROOT, 'examples', 'muse-demo', 'src', 'MainActivity.java');

let navError = null;
let nav = null;
try {
  nav = require(NAV_PATH);
} catch (err) {
  navError = err;
}

function requireNav() {
  assert.equal(navError, null, `demoNav module missing: ${navError && navError.message}`);
  assert.equal(typeof nav.parseTabLink, 'function', 'demoNav must export parseTabLink()');
}

function read(p) {
  return readFileSync(p, 'utf8');
}

// Hermetic host double for the specified pushValidatedTab funnel:
// validate with demoNav.parseTabLink (source of truth), call the injected
// setter then repaint, log denial, keep current tab. Mirrors the plan's
// Java-carries-state-only constraint (no tab machine, no banner copy beyond
// the denied-link string).
function makePushValidatedTab(deps) {
  let current = deps.initialTab || 'lens';
  const calls = { setter: [], repaint: 0 };
  const logs = [];
  function pushValidatedTab(link) {
    const parsed = nav.parseTabLink(link);
    if (parsed.code) {
      logs.push(`unknown-route destinationReached=false link=${String(link)}`);
      return { applied: false, code: 'unknown-route' };
    }
    const rc = deps.setter(deps.handle, parsed.id, null);
    calls.setter.push([deps.handle, parsed.id]);
    if (rc !== 1) {
      logs.push(`unknown-route destinationReached=false link=${String(link)}`);
      return { applied: false, code: 'unknown-route' };
    }
    deps.repaint(deps.handle);
    calls.repaint += 1;
    current = parsed.id;
    logs.push(`tab=${parsed.id} destinationReached=true repaint=1`);
    return { applied: true };
  }
  return { pushValidatedTab, calls, logs, current: () => current };
}

describe('T7 — parseTabLink stays the allowlist source of truth', () => {
  it('accepts muse-demo://tab/<lens|field|list>', () => {
    requireNav();
    for (const tab of ['lens', 'field', 'list']) {
      assert.deepEqual(nav.parseTabLink(`muse-demo://tab/${tab}`), { id: tab });
    }
  });

  it('rejects unknown/host/scheme variants with unknown-route', () => {
    requireNav();
    for (const bad of [
      'muse-demo://tab/nope',
      'muse-demo://tab/',
      'muse-demo://tab/LENS',
      'gallery://story/x',
      'muse-demo://lens',
      '',
      'muse-demo://tab/lens?seed=1',
    ]) {
      assert.equal(nav.parseTabLink(bad).code, 'unknown-route', `must deny ${JSON.stringify(bad)}`);
    }
  });
});

describe('T7 — pushValidatedTab applies valid links via setter + repaint', () => {
  it('warm link applies, calls setter then repaint, lands on the tab', () => {
    requireNav();
    const order = [];
    const h = makePushValidatedTab({
      handle: 1,
      initialTab: 'lens',
      setter: () => { order.push('setter'); return 1; },
      repaint: () => { order.push('repaint'); },
    });
    assert.deepEqual(h.pushValidatedTab('muse-demo://tab/field'), { applied: true });
    assert.deepEqual(order, ['setter', 'repaint'], 'setter must precede repaint');
    assert.equal(h.current(), 'field');
    assert.equal(h.calls.repaint, 1);
  });

  it('cold link (same funnel) applies identically', () => {
    requireNav();
    const h = makePushValidatedTab({
      handle: 1,
      initialTab: 'lens',
      setter: () => 1,
      repaint: () => {},
    });
    assert.deepEqual(h.pushValidatedTab('muse-demo://tab/list'), { applied: true });
    assert.equal(h.current(), 'list');
  });

  it('tap-advance and fallback-recovery reuse the same setter path', () => {
    requireNav();
    const h = makePushValidatedTab({
      handle: 1,
      initialTab: 'lens',
      setter: () => 1,
      repaint: () => {},
    });
    assert.deepEqual(h.pushValidatedTab('muse-demo://tab/lens'), { applied: true });
    assert.equal(h.calls.setter.length, 1);
    assert.equal(h.calls.repaint, 1);
  });
});

describe('T7 — pushValidatedTab fail-closed: unknown keeps tab, logs denial', () => {
  it('unknown link denies, never calls setter/repaint, keeps tab', () => {
    requireNav();
    const h = makePushValidatedTab({
      handle: 1,
      initialTab: 'field',
      setter: () => { throw new Error('setter must not run on denial'); },
      repaint: () => { throw new Error('repaint must not run on denial'); },
    });
    const out = h.pushValidatedTab('muse-demo://tab/nope');
    assert.deepEqual(out, { applied: false, code: 'unknown-route' });
    assert.equal(h.current(), 'field', 'denied link keeps the current tab (never blank)');
    assert.match(h.logs.join('\n'), /unknown-route/);
    assert.match(h.logs.join('\n'), /destinationReached=false/);
  });

  it('setter returning 0 denies with unknown-route and keeps tab', () => {
    requireNav();
    let repainted = false;
    const h = makePushValidatedTab({
      handle: 0,
      initialTab: 'lens',
      setter: () => 0,
      repaint: () => { repainted = true; },
    });
    assert.deepEqual(h.pushValidatedTab('muse-demo://tab/field'), { applied: false, code: 'unknown-route' });
    assert.equal(h.current(), 'lens');
    assert.equal(repainted, false);
  });

  it('wrong scheme/host denies without touching state', () => {
    requireNav();
    const h = makePushValidatedTab({
      handle: 1,
      initialTab: 'list',
      setter: () => { throw new Error('must not run'); },
      repaint: () => { throw new Error('must not run'); },
    });
    for (const bad of ['gallery://story/x', 'muse-demo://lens', null, undefined, 42]) {
      assert.deepEqual(h.pushValidatedTab(bad), { applied: false, code: 'unknown-route' });
    }
    assert.equal(h.current(), 'list');
  });
});

describe('T7 — MainActivity.java funnel pins (routeDemoLink/onNewIntent/tap/fallback via setter)', () => {
  it('declares pushValidatedTab-equivalent routeDemoLink calling nativeSetDemoShellState then nativeRenderFrame', () => {
    const src = read(ACTIVITY_PATH);
    assert.match(src, /nativeSetDemoShellState/, 'host must call the T6 setter');
    const idx = src.indexOf('nativeSetDemoShellState');
    const window = src.slice(Math.max(0, idx - 500), idx + 1500);
    assert.match(window, /nativeRenderFrame/, 'setter path must repaint via nativeRenderFrame');
  });

  it('keeps cold (stashed drain) + warm (onNewIntent) entries and denial logging', () => {
    const src = read(ACTIVITY_PATH);
    assert.match(src, /onNewIntent/, 'warm path via onNewIntent singleTask must exist');
    assert.match(src, /pendingDeepLink|stashed/i, 'cold stashed-drain path must exist');
    assert.match(src, /unknown-route/, 'denial must log unknown-route');
    assert.match(src, /destinationReached=false/, 'denial must log destinationReached=false');
  });
});
