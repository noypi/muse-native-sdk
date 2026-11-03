'use strict';
// T9 — Thin host opaque-forward + app-adapter bridge binding (AC-NAV-041/044).
// Hermetic: node:test + node:assert/strict only. Static source pins over
// `examples/muse-demo/src/MainActivity.java` (opaque stash / take-and-clear
// drain / unparsed forward, no parsing, no demo setter) plus hermetic
// doubles over `examples/muse-demo/src/demoNav.js` (`ensureRouterBridge`
// first-wins binding, `drainStashedOpaqueLink` take-and-clear once,
// unbound fail-closed denials). No device, no downloads, no secrets, no
// config, no host-env mutation. No production code touched.
//
// Supersedes the retired `pushValidatedTab` funnel pins: the host no longer
// validates tabs or calls a demo setter + repaint; JS route validity lives
// in the route table + guards behind the single `RouterBridge.invoke` path.
// Kept intent: take-and-clear (consume exactly once) and fail-closed
// denials (named code, page/state unchanged, never throw).
//
// ASSUMPTION A1: the hermetic doubles below mirror the specified funnel
// (opaque stash -> take-and-clear consume -> forwardIntent -> RouterBridge);
// the real MainActivity.java / demoNav.js paths satisfy the same observable
// contract + pins.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const RUN_ROOT = join(__dirname, '..', '..', '..');
const ACTIVITY_PATH = join(RUN_ROOT, 'examples', 'muse-demo', 'src', 'MainActivity.java');
const NAV_PATH = '../src/demoNav.js';

function read(p) {
  return readFileSync(p, 'utf8');
}

let navError = null;
let nav = null;
try {
  nav = require(NAV_PATH);
} catch (err) {
  navError = err;
}

function requireNav() {
  assert.equal(navError, null, `demoNav module missing: ${navError && navError.message}`);
}

describe('T9 — MainActivity stays opaque-only (no parsing, no demo setter)', () => {
  it('declares no demo seam (nativeSetDemoShellState/routeDemoLink/parseTabLink absent)', () => {
    const src = read(ACTIVITY_PATH);
    assert.ok(!src.includes('nativeSetDemoShellState'), 'host must not call the retired demo setter');
    assert.ok(!src.includes('routeDemoLink'), 'host must not carry the retired routeDemoLink funnel');
    assert.ok(!src.includes('parseTabLink'), 'host must never parse links (validity lives in JS)');
  });

  it('stashes inbound intents opaque with a bounded single slot and take-and-clear drain', () => {
    const src = read(ACTIVITY_PATH);
    assert.match(src, /stashOpaqueIntent/, 'host must stash inbound intents opaque');
    assert.match(src, /consumePendingOpaqueLink/, 'host must expose a take-and-clear drain');
    assert.match(src, /pendingOpaqueLink = null/, 'drain must clear the slot (take-and-clear)');
    assert.match(src, /MAX_OPAQUE_LINK_CHARS/, 'stash must stay bounded (oversize dropped fail-closed)');
  });
});

describe('T9 — MainActivity forwards opaque via the sink on both entries (take-and-clear kept)', () => {
  it('cold (onCreate) + warm (onNewIntent) arrivals drain unparsed through forwardOpaqueToRouter', () => {
    const src = read(ACTIVITY_PATH);
    assert.match(src, /forwardOpaqueToRouter/, 'host must hand the taken string to the router channel');
    assert.match(src, /onNewIntent/, 'warm path via onNewIntent singleTask must exist');
    assert.match(src, /installDefaultOpaqueLinkSink/, 'cold path must install the live sink before draining');
    assert.match(src, /drainPendingOpaqueLink/, 'both entries must drain the stash once ready');
  });

  it('sink stores the taken string unparsed and never crashes the host (fail-closed)', () => {
    const src = read(ACTIVITY_PATH);
    assert.match(src, /opaqueLinkSink/, 'host must deliver through the injected sink seam');
    assert.match(src, /lastForwardedOpaqueLink/, 'sink retains the taken opaque string unparsed');
    assert.match(src, /opaque-sink-accept/, 'delivery must stay observable in logcat');
    const idx = src.indexOf('forwardOpaqueToRouter(String opaqueLink)');
    assert.ok(idx >= 0, 'forward entry must exist (companion pin)');
    const window = src.slice(idx, idx + 1200);
    assert.match(window, /catch \(Throwable ignored\)/, 'a bad sink must fail closed, never crash the host');
  });
});

describe('T9 — demoNav binds the bridge at boot (first-wins, never throws)', () => {
  it('ensureRouterBridge installs a default RouterBridge without clobbering an existing one', () => {
    requireNav();
    assert.equal(typeof nav.ensureRouterBridge, 'function', 'demoNav must export ensureRouterBridge()');
    const had = globalThis.RouterBridge;
    try {
      delete globalThis.RouterBridge;
      assert.equal(nav.ensureRouterBridge(), true, 'binding must install the default bridge');
      assert.equal(typeof globalThis.RouterBridge.invoke, 'function', 'default bridge must expose invoke');
      const installed = globalThis.RouterBridge;
      assert.equal(nav.ensureRouterBridge(), true, 're-bind must stay fail-closed');
      assert.equal(globalThis.RouterBridge, installed, 'first-wins: an installed bridge keeps priority');
    } finally {
      if (had === undefined) delete globalThis.RouterBridge;
      else globalThis.RouterBridge = had;
    }
  });

  it(' bound bridge resolves invoke in-memory so demoTransport() is non-null at boot', async () => {
    requireNav();
    const had = globalThis.RouterBridge;
    try {
      delete globalThis.RouterBridge;
      nav.ensureRouterBridge();
      const bridge = globalThis.RouterBridge;
      assert.ok(bridge && typeof bridge.invoke === 'function', 'bridge must be bound after boot');
      const res = await bridge.invoke('navigate', { route: 'lens' });
      assert.ok(res && typeof res === 'object', 'default invoke must resolve a result object');
    } finally {
      if (had === undefined) delete globalThis.RouterBridge;
      else globalThis.RouterBridge = had;
    }
  });
});

describe('T9 — take-and-clear + fail-closed denials kept (drainStashedOpaqueLink / unbound router)', () => {
  it('drain consumes the stash exactly once and forwards through forwardIntent', async () => {
    requireNav();
    let takes = 0;
    const seen = [];
    const router = {
      forwardIntent: async (opaque) => {
        seen.push(opaque);
        return { ok: true };
      },
    };
    const out = await nav.drainStashedOpaqueLink(router, () => {
      takes += 1;
      return 'muse-demo://tab/field';
    });
    assert.deepEqual(out, { ok: true });
    assert.equal(takes, 1, 'stash must be taken exactly once');
    assert.deepEqual(seen, ['muse-demo://tab/field']);
    const empty = await nav.drainStashedOpaqueLink(router, () => null);
    assert.equal(empty, null, 'nothing stashed must yield null (never a denial, never a throw)');
  });

  it('unbound push/forwardIntent fail closed with a named code, page unchanged, never throw', async () => {
    requireNav();
    const router = nav.createDemoRouter({ transport: undefined });
    const before = router.current;
    const r1 = await router.push('lens');
    assert.equal(r1.ok, false, 'unbound push must fail closed, never {ok:true} silent no-op');
    assert.ok(typeof r1.code === 'string' && r1.code.length > 0, 'denial must carry a named code');
    assert.equal(router.current, before, 'denial leaves current unchanged');
    const r2 = await router.forwardIntent('muse-demo://tab/lens');
    assert.equal(r2.ok, false, 'unbound forwardIntent must fail closed');
    assert.equal(router.current, before, 'denial leaves current unchanged');
  });
});
