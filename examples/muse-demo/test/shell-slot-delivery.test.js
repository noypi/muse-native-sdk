'use strict';
// Batch 1 RED pins — nav-shell-delivery T1+T2 (GR-08).
// Hermetic: node:test + node:assert/strict only. Static source pins over
// `examples/muse-demo/src/MainActivity.java` plus a hermetic JS mirror of
// the CURRENT (log-only) delivery path. No device, no network, no secrets,
// no config, no host-env mutation. No production code touched.
//
// T1 (plan Interface: `forwardOpaqueToRouter(opaque:String): void` must
// invoke `nativeSetShellState(route,notice): void`; hermetic analog
// asserting the `shell-state route=` marker proves CALLED):
//   - T1.1 static: forwardOpaqueToRouter body contains nativeSetShellState.
//   - T1.2 model: mirrored forward produces the shell-state route= marker.
//   Both FAIL today: forward only hits the log-only OpaqueLinkSink +
//   `opaque-intent-ready` log; nothing calls nativeSetShellState.
//
// T2 (plan Interface: `stashOpaqueIntent(intent): void;
// drainPendingOpaqueLink(): void` — warm AND cold VIEW must carry the `-d`
// URI; START line recorded):
//   - T2-cold / T2-warm: the exact START-line shape the device legs must
//     record, then stash→drain→router→slot end-to-end for that URI.
//     Shape pins hold; slot-delivery asserts FAIL today (log-only sink).
//   - T2-uri: static pin that the `-d muse-demo://tab/<lens|field|list|home>`
//     contract is present at the host seam. FAILs today (host never names
//     the URI; dataless VIEW / stash drop undocumented).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const DEMO_ROOT = join(__dirname, '..');
const MAIN_PATH = join(DEMO_ROOT, 'src', 'MainActivity.java');

function readMain() {
  return readFileSync(MAIN_PATH, 'utf8');
}

// Window of a Java method body starting at its declaration match.
function methodWindow(src, declRe, len = 1400) {
  const m = src.match(declRe);
  assert.ok(m, `MainActivity.java must declare ${declRe}`);
  const idx = src.indexOf(m[0]);
  return src.slice(idx, idx + len);
}

// Hermetic mirror of the CURRENT MainActivity delivery path (log-only):
// stash keeps the raw opaque string; forward hits the sink + log only and
// never touches the shell slot. Mirrors MainActivity.java as of Batch 1.
function currentHost() {
  const state = {
    pendingOpaqueLink: null,
    sinkCalls: [],
    shellCalls: [],
    logs: [],
  };
  const MAX_OPAQUE_LINK_CHARS = 2048;
  function stashOpaqueIntent(dataString) {
    const raw = dataString;
    if (raw == null || raw === '') return;
    if (raw.length > MAX_OPAQUE_LINK_CHARS) return;
    state.pendingOpaqueLink = raw;
  }
  function consumePendingOpaqueLink() {
    const link = state.pendingOpaqueLink;
    state.pendingOpaqueLink = null;
    return link;
  }
  function forwardOpaqueToRouter(opaqueLink) {
    if (opaqueLink == null || opaqueLink === '') return;
    // CURRENT behavior: log-only sink, no nativeSetShellState call.
    state.sinkCalls.push(opaqueLink);
    state.logs.push('opaque-intent-ready engineHosted=true');
  }
  function drainPendingOpaqueLink(engineReady) {
    if (state.pendingOpaqueLink == null || !engineReady) return;
    forwardOpaqueToRouter(consumePendingOpaqueLink());
  }
  return { state, stashOpaqueIntent, consumePendingOpaqueLink, forwardOpaqueToRouter, drainPendingOpaqueLink };
}

// Exact START-line shape the warm + cold device legs must record
// (ActivityTaskManager VIEW dispatch with the `-d` URI as `dat=`).
function startLine(tab) {
  return (
    `START u0 {act=android.intent.action.VIEW dat=muse-demo://tab/${tab} ` +
    `flg=0x10000000 cmp=com.muse.host/.MainActivity} with LAUNCH_SINGLE_TASK from uid 2000`
  );
}
const START_SHAPE =
  /^START u0 \{act=android\.intent\.action\.VIEW dat=muse-demo:\/\/tab\/(lens|field|list|home) flg=0x10000000 cmp=com\.muse\.host\/\.MainActivity\} with LAUNCH_SINGLE_TASK/;

describe('T1 — forwardOpaqueToRouter resolves to nativeSetShellState (RED pre-wire)', () => {
  it('T1.1 forwardOpaqueToRouter body invokes nativeSetShellState (not the log-only sink)', () => {
    const src = readMain();
    const body = methodWindow(src, /void forwardOpaqueToRouter\(String opaqueLink\)/);
    assert.match(
      body,
      /nativeSetShellState/,
      'forwardOpaqueToRouter must call nativeSetShellState(route,notice); currently log-only sink',
    );
  });

  it('T1.2 delivery emits the shell-state route= marker (proves slot CALLED)', () => {
    const host = currentHost();
    host.stashOpaqueIntent('muse-demo://tab/lens');
    host.drainPendingOpaqueLink(true);
    assert.equal(host.state.sinkCalls.length, 1, 'opaque must reach the sink (companion pin)');
    // Desired: the forward resolves to nativeSetShellState, surfaced by the
    // `shell-state route=` marker. Current log-only path never records it.
    const markerFired =
      host.state.shellCalls.length === 1 &&
      host.state.logs.some((l) => l.includes('shell-state route='));
    assert.ok(markerFired, 'forward must resolve to nativeSetShellState; want one shell-state route= marker');
  });
});

describe('T2 — VIEW-intent-consumed contract warm+cold (RED pre-wire)', () => {
  it('T2-cold cold VIEW START line carries the -d URI through stash/drain to the slot', () => {
    const line = startLine('lens');
    assert.match(line, START_SHAPE, 'cold leg must record the exact START-line shape with dat=muse-demo://tab/lens');
    const host = currentHost();
    host.stashOpaqueIntent('muse-demo://tab/lens'); // cold onCreate stash
    host.drainPendingOpaqueLink(true); // cold drain once engine ready
    assert.equal(host.state.pendingOpaqueLink, null, 'cold drain consumes the stash exactly once');
    assert.equal(host.state.sinkCalls[0], 'muse-demo://tab/lens', 'cold -d URI flows verbatim to the router');
    assert.equal(
      host.state.shellCalls.length,
      1,
      'cold VIEW -d URI must reach nativeSetShellState via forwardOpaqueToRouter (log-only today)',
    );
  });

  it('T2-warm warm VIEW (onNewIntent) START line carries the -d URI through stash/drain to the slot', () => {
    const line = startLine('field');
    assert.match(line, START_SHAPE, 'warm leg must record the exact START-line shape with dat=muse-demo://tab/field');
    const host = currentHost();
    host.stashOpaqueIntent('muse-demo://tab/field'); // warm onNewIntent stash
    host.drainPendingOpaqueLink(true); // warm drain
    assert.equal(host.state.pendingOpaqueLink, null, 'warm drain consumes the stash exactly once');
    assert.equal(host.state.sinkCalls[0], 'muse-demo://tab/field', 'warm -d URI flows verbatim to the router');
    assert.equal(
      host.state.shellCalls.length,
      1,
      'warm VIEW -d URI must reach nativeSetShellState via forwardOpaqueToRouter (log-only today)',
    );
  });

  it('T2-uri host seam names the -d muse-demo://tab/<tab> contract (dataless VIEW drops today)', () => {
    const src = readMain();
    assert.match(
      src,
      /muse-demo:\/\/tab\//,
      'host seam must name the -d muse-demo://tab/<lens|field|list|home> contract; dataless VIEW currently drops silently',
    );
  });
});
