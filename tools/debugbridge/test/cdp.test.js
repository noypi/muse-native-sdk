'use strict';
// T7 — FR-DX-009 / AC-DX-039 / TC-DX-039 (Batch 3).
// Hermetic: node:test + node:assert/strict only. The Hermes CDP transport and
// the TS→bytecode source-map resolver are fakes injected in-process; no
// device, no socket, no network, no secrets, no config.
// Interface under test (plan verbatim):
//   createCdpBridge(opts: {dialect: 'hermes'}) => {setBreakpoint(params:
//   object), stepOver(params: object), resume(), evaluateOnCallFrame(params:
//   object), evaluate(params: object), takeHeapSnapshot(), startCoverage()}
//   — each returns a Promise resolving to the CDP result; unsupported Hermes
//   methods reject with {command, cause: "unsupported by Hermes CDP",
//   action}; stacks return source-mapped frames.
//   from tools/debugbridge/lib/cdp-bridge.js.
// ASSUMPTION (D2): full CDP via the Hermes dialect only — dialect must be
// 'hermes'; any other dialect rejects with a structured error. Panels Hermes
// cannot serve reject with {command, cause: "unsupported by Hermes CDP",
// action} naming DX-009.
// ASSUMPTION: tests inject a fake transport + fake source map through
// optional `transport` / `sourceMap` fields (or `__setTransport` /
// `__setSourceMap` hooks if the field form is absent). These hooks are
// test-only; production wires the real Hermes transport. Tests probe the
// field form first, then the hook form.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const BRIDGE = path.join(__dirname, '..', 'lib', 'cdp-bridge.js');

function fresh(modPath) {
  delete require.cache[require.resolve(modPath)];
  return require(modPath);
}

// Fake Hermes CDP transport: records sends, resolves canned results.
function makeFakeTransport(overrides) {
  const sent = [];
  const cfg = overrides || {};
  return {
    sent,
    async send(method, params) {
      sent.push({ method, params });
      if (cfg.unsupportedMethods && cfg.unsupportedMethods.has(method)) {
        const err = new Error('unsupported by Hermes CDP (DX-009)');
        err.code = 'UNSUPPORTED_BY_HERMES';
        err.command = method;
        throw err;
      }
      if (method === 'Debugger.getStackTrace' || method === 'Debugger.paused') {
        return {
          callFrames: [
            { functionName: 'onPress', url: 'bundle.js', lineNumber: 42, columnNumber: 7 },
          ],
        };
      }
      if (method === 'Heap.takeHeapSnapshot') return { snapshotId: 'snap-1', nodeCount: 10 };
      if (method === 'Profiler.startPreciseCoverage') return { started: true };
      return { ok: true, method, params };
    },
  };
}

// Fake TS→bytecode source-map resolver: bundle.js:42 -> App.tsx:10.
function makeFakeSourceMap() {
  return {
    originalPositionFor(generated) {
      assert.ok(generated && typeof generated.url === 'string');
      if (generated.url === 'bundle.js') {
        return { source: 'src/App.tsx', line: 10, column: 3 };
      }
      return { source: null, line: null, column: null };
    },
  };
}

function createWithFakes(freshMod, transport, sourceMap) {
  const { createCdpBridge } = freshMod;
  const opts = { dialect: 'hermes', transport, sourceMap };
  let bridge;
  try {
    bridge = createCdpBridge(opts);
  } catch (err) {
    // Field-injection not accepted: fall back to hook form.
    bridge = freshMod.createCdpBridge({ dialect: 'hermes' });
    if (typeof bridge.__setTransport === 'function') bridge.__setTransport(transport);
    else if (typeof freshMod.__setTransport === 'function') freshMod.__setTransport(transport);
    if (typeof bridge.__setSourceMap === 'function') bridge.__setSourceMap(sourceMap);
    else if (typeof freshMod.__setSourceMap === 'function') freshMod.__setSourceMap(sourceMap);
  }
  // Hooks also applied when present so both forms stay wired.
  if (bridge && typeof bridge.__setTransport === 'function' && bridge.__transport !== transport) {
    try { bridge.__setTransport(transport); } catch { /* field form already wired */ }
  }
  return bridge;
}

const EXPECTED_METHODS = [
  'setBreakpoint',
  'stepOver',
  'resume',
  'evaluateOnCallFrame',
  'evaluate',
  'takeHeapSnapshot',
  'startCoverage',
];

describe('cdp bridge surface (T7 / TC-DX-039)', () => {
  it('exposes createCdpBridge creating the 7-method Hermes bridge', () => {
    const mod = fresh(BRIDGE);
    assert.equal(typeof mod.createCdpBridge, 'function');
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    for (const name of EXPECTED_METHODS) {
      assert.equal(typeof bridge[name], 'function', `${name} must be a function`);
    }
  });

  it('each method returns a Promise', () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    const calls = [
      bridge.setBreakpoint({ url: 'bundle.js', lineNumber: 42 }),
      bridge.stepOver({}),
      bridge.resume(),
      bridge.evaluateOnCallFrame({ callFrameId: '0', expression: '1+1' }),
      bridge.evaluate({ expression: '1+1' }),
      bridge.takeHeapSnapshot(),
      bridge.startCoverage(),
    ];
    for (const p of calls) {
      assert.ok(p && typeof p.then === 'function', 'each method must return a Promise');
    }
    return Promise.all(calls).then(() => undefined);
  });

  it('setBreakpoint resolves with the CDP result', async () => {
    const mod = fresh(BRIDGE);
    const transport = makeFakeTransport();
    const bridge = createWithFakes(mod, transport, makeFakeSourceMap());
    const res = await bridge.setBreakpoint({ url: 'bundle.js', lineNumber: 42 });
    assert.ok(res && typeof res === 'object', 'must resolve to the CDP result object');
    assert.ok(transport.sent.length > 0, 'fake transport must observe the request');
  });

  it('stepOver then resume resolve in order (stepping)', async () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    const stepped = await bridge.stepOver({});
    assert.ok(stepped && typeof stepped === 'object');
    const resumed = await bridge.resume();
    assert.ok(resumed && typeof resumed === 'object');
  });

  it('REPL evaluateOnCallFrame and evaluate resolve values', async () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    const onFrame = await bridge.evaluateOnCallFrame({ callFrameId: '0', expression: 'x + 1' });
    assert.ok(onFrame && typeof onFrame === 'object');
    const global = await bridge.evaluate({ expression: '1 + 2' });
    assert.ok(global && typeof global === 'object');
  });

  it('stacks return source-mapped frames (bundle.js -> src/App.tsx)', async () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    const res = await bridge.stepOver({});
    const text = JSON.stringify(res);
    // Source-mapped frames must name the original TS source, never only bytecode.
    assert.ok(/App\.tsx/.test(text), `stack must carry source-mapped frame, got: ${text}`);
  });

  it('takeHeapSnapshot resolves with a snapshot result', async () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    const snap = await bridge.takeHeapSnapshot();
    assert.ok(snap && typeof snap === 'object');
    assert.ok(/snap/.test(JSON.stringify(snap)), 'snapshot result must identify the snapshot');
  });

  it('startCoverage resolves with a coverage result', async () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    const cov = await bridge.startCoverage();
    assert.ok(cov && typeof cov === 'object');
  });

  it('unsupported Hermes methods reject with {command, cause, action}', async () => {
    const mod = fresh(BRIDGE);
    const bridge = createWithFakes(mod, makeFakeTransport(), makeFakeSourceMap());
    // Prefer an explicit unsupported-panel entry point when exposed.
    const probe =
      (typeof bridge.unsupportedPanel === 'function' && (() => bridge.unsupportedPanel('Profiler.takePreciseCoverage'))) ||
      (typeof bridge.sendUnsupported === 'function' && (() => bridge.sendUnsupported('Profiler.takePreciseCoverage'))) ||
      null;
    let err = null;
    if (probe) {
      try { await probe(); } catch (e) { err = e; }
    } else {
      // Otherwise drive an unsupported domain through the fake transport.
      const faulting = makeFakeTransport({ unsupportedMethods: new Set(['Profiler.takePreciseCoverage']) });
      const b2 = createWithFakes(fresh(BRIDGE), faulting, makeFakeSourceMap());
      const c2 = (typeof b2.unsupportedPanel === 'function' && (() => b2.unsupportedPanel('Profiler.takePreciseCoverage'))) || null;
      try {
        if (c2) await c2();
        else if (typeof b2.evaluate === 'function') {
          // Force the unsupported path via a Hermes-unsupported expression domain marker.
          await b2.evaluate({ expression: '@unsupported-panel:Profiler.takePreciseCoverage' });
          // If the bridge resolved it instead of rejecting, fail explicitly.
          assert.fail('expected unsupported Hermes method to reject');
        }
      } catch (e) { err = e; }
    }
    assert.ok(err, 'unsupported Hermes method must reject, not resolve');
    assert.equal(typeof err.command, 'string');
    assert.equal(err.cause, 'unsupported by Hermes CDP');
    assert.equal(typeof err.action, 'string');
    assert.ok(err.action.length > 0, 'action must name the DX-009 fallback');
    assert.ok(/DX-009/.test(err.action), 'action must name DX-009');
  });

  it('non-hermes dialect is rejected (D2: Hermes dialect only)', async () => {
    const mod = fresh(BRIDGE);
    await assert.rejects(
      async () => mod.createCdpBridge({ dialect: 'v8' }),
      (err) => err && typeof err.command === 'string' && typeof err.cause === 'string',
      'non-hermes dialect must reject with a structured error',
    );
  });
});

describe('cdp bridge failure-path hardening (review MAJORs)', () => {
  it('no-transport bridge rejects resume() with a missing-transport config error', async () => {
    const mod = fresh(BRIDGE);
    // No transport field and no __setTransport call: an unconfigured bridge.
    const bridge = mod.createCdpBridge({ dialect: 'hermes' });
    let err = null;
    try {
      await bridge.resume();
    } catch (e) { err = e; }
    assert.ok(err, 'resume() on a transport-less bridge must reject, not resolve');
    assert.ok(
      err.cause !== 'unsupported by Hermes CDP',
      `missing transport must not be reported as Hermes-unsupported, got cause: ${JSON.stringify(err && err.cause)}`,
    );
    assert.ok(
      /transport/i.test(String(err.cause)),
      `config error must name the missing transport, got cause: ${JSON.stringify(err && err.cause)}`,
    );
  });

  it('stepOver with unsupported stack fetch never invents bundle.js:42 frames', async () => {
    const mod = fresh(BRIDGE);
    // Only Debugger.getStackTrace is unsupported; stepOver itself resolves.
    const faulting = makeFakeTransport({ unsupportedMethods: new Set(['Debugger.getStackTrace']) });
    const bridge = createWithFakes(fresh(BRIDGE), faulting, makeFakeSourceMap());
    let res = null;
    let err = null;
    try {
      res = await bridge.stepOver({});
    } catch (e) { err = e; }
    if (err) {
      // Propagating the unsupported error is acceptable.
      assert.equal(err.cause, 'unsupported by Hermes CDP');
      return;
    }
    // Otherwise the step result must not carry invented canonical frames.
    const text = JSON.stringify(res);
    assert.ok(
      !/bundle\.js:42/.test(text) && !/App\.tsx/.test(text),
      `step result must not carry invented stack frames, got: ${text}`,
    );
  });
});
