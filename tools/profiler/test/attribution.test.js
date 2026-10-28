'use strict';
// T9 — FR-DX-004 / AC-DX-018 + AC-DX-019 / TC-DX-018 + TC-DX-019 (Batch 4).
// Hermetic: node:test + node:assert/strict only. Sessions, faults, and
// exports are plain objects built inline — no device, no Perfetto binary,
// no GPU/Vulkan, no network, no clock, no config.
// Interfaces under test (plan verbatim):
//   attachRetainedViews(session) => { dag, layoutTree, signalGraph, vulkanTimings }
//   verifyAttribution(exported, fault) => { attributedTo }
// Retained views are thin adapters re-exposing T1-layout, T2-signal, and
// T6-frame shapes inside the profiler timeline (no new capture).
// Fault shape under test (ASSUMPTION, see contract):
//   allocation fault = { kind: 'allocation', subsystem, bytesPerFrame }
//   render-pass fault = { kind: 'render-pass', passName, subsystem, costMs }
// `exported` is the T8 export shape:
//   { trace: { format: 'perfetto-json-v1', events }, manifest: {...} }.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'retained-views.js');

function load() {
  return require(LIB);
}

function session(sessionId) {
  return {
    sessionId,
    frames: [{ passes: [{ name: 'render', subsystem: 'engine', costMs: 24 }] }],
    snapshots: [{ engineBytes: 10, jsHeapBytes: 50, gcEvents: 2, vramBytes: 10, nativeHeapBytes: 10 }],
  };
}

function exportedTrace(sessionId) {
  return {
    trace: {
      format: 'perfetto-json-v1',
      events: [{ name: 'render', subsystem: 'engine', costMs: 24 }],
    },
    manifest: { device: 'test-device', os: 'test-os-1.0', engineVersion: '0.0.0-test', sessionId },
  };
}

function assertNoLeak(err) {
  assert.ok(err instanceof Error, 'must throw an Error');
  assert.doesNotMatch(String(err.stack ?? '').split('\n').slice(1).join('\n'), /node_modules/);
  return true;
}

describe('attachRetainedViews', () => {
  it('exposes all four retained views in the same session timeline', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(session('s1'));
    for (const key of ['dag', 'layoutTree', 'signalGraph', 'vulkanTimings']) {
      assert.ok(out[key] !== undefined && out[key] !== null, `${key} must be present`);
    }
  });

  it('exposes a render DAG (nodes plus edges)', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(session('s1'));
    assert.ok(out.dag && typeof out.dag === 'object', 'dag must be an object');
  });

  it('exposes a layout tree view', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(session('s1'));
    assert.ok(out.layoutTree && typeof out.layoutTree === 'object', 'layoutTree must be an object');
  });

  it('exposes a signal dependency graph view', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(session('s1'));
    assert.ok(out.signalGraph && typeof out.signalGraph === 'object', 'signalGraph must be an object');
  });

  it('exposes vulkan frame timings', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(session('s1'));
    assert.ok(Array.isArray(out.vulkanTimings) || (out.vulkanTimings && typeof out.vulkanTimings === 'object'), 'vulkanTimings must be present');
  });

  it('rejects a malformed session without leaking internals (ASSUMPTION)', () => {
    const { attachRetainedViews } = load();
    for (const bad of [null, undefined, 's1', 42, {}]) {
      assert.throws(() => attachRetainedViews(bad), assertNoLeak);
    }
  });
});

describe('verifyAttribution', () => {
  it('attributes an injected per-frame allocation fault to its subsystem', () => {
    const { verifyAttribution } = load();
    const fault = { kind: 'allocation', subsystem: 'engine', bytesPerFrame: 1024 };
    const out = verifyAttribution(exportedTrace('s1'), fault);
    assert.equal(out.attributedTo, 'engine');
  });

  it('attributes an injected extra render pass to its subsystem', () => {
    const { verifyAttribution } = load();
    const fault = { kind: 'render-pass', passName: 'shadow-blur', subsystem: 'gpu', costMs: 40 };
    const out = verifyAttribution(exportedTrace('s1'), fault);
    assert.equal(out.attributedTo, 'gpu');
  });

  it('distinguishes the two injected faults (allocation vs render pass)', () => {
    const { verifyAttribution } = load();
    const alloc = verifyAttribution(exportedTrace('s1'), { kind: 'allocation', subsystem: 'platform', bytesPerFrame: 512 });
    const pass = verifyAttribution(exportedTrace('s1'), { kind: 'render-pass', passName: 'blur', subsystem: 'js', costMs: 30 });
    assert.equal(alloc.attributedTo, 'platform');
    assert.equal(pass.attributedTo, 'js');
    assert.notEqual(alloc.attributedTo, pass.attributedTo);
  });

  it('rejects a malformed fault without leaking internals (ASSUMPTION)', () => {
    const { verifyAttribution } = load();
    for (const bad of [null, undefined, {}, { kind: 'allocation' }, { kind: 'nope', subsystem: 'engine' }]) {
      assert.throws(() => verifyAttribution(exportedTrace('s1'), bad), assertNoLeak);
    }
  });

  it('rejects a malformed export without leaking internals (ASSUMPTION)', () => {
    const { verifyAttribution } = load();
    const fault = { kind: 'allocation', subsystem: 'engine', bytesPerFrame: 64 };
    for (const bad of [null, undefined, {}, { trace: null }]) {
      assert.throws(() => verifyAttribution(bad, fault), assertNoLeak);
    }
  });
});
