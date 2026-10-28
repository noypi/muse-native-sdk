'use strict';
// T9 — FR-DX-004 / AC-DX-018 + AC-DX-019 / TC-DX-019 (Batch 4).
// Hermetic: node:test + node:assert/strict only. Sessions, faults, and
// exports are plain objects built inline — no device, no Perfetto binary,
// no GPU/Vulkan, no network, no clock, no config.
// Interfaces under test (plan verbatim):
//   attachRetainedViews(session) => { dag, layoutTree, signalGraph, vulkanTimings }
//   verifyAttribution(exported, fault) => { attributedTo }
// Retained views are thin adapters re-exposing T1-layout, T2-signal, and
// T6-frame shapes inside the profiler timeline (no new capture).
// Fault shapes under test:
//   allocation fault = { kind: 'allocation', subsystem, bytesPerFrame }
//   render-pass fault = { kind: 'render-pass', passName, subsystem, costMs }
// `exported` is the T8 export shape:
//   { trace: { format: 'perfetto-json-v1', events }, manifest: {...} }.
// Fault-signature rule (implementation contract): allocation faults match a
// trace event carrying allocation byte evidence (>= bytesPerFrame in
// bytes/bytesPerFrame/size/arena fields) for the fault subsystem; render
// pass faults match a trace event whose name equals passName for the fault
// subsystem. No evidence => E_PROFILER_ATTRIBUTION_MISMATCH.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'retained-views.js');

function load() {
  return require(LIB);
}

function session(sessionId, overrides = {}) {
  return {
    sessionId,
    frames: [{ passes: [{ name: 'render', subsystem: 'engine', costMs: 24 }] }],
    snapshots: [{ engineBytes: 10, jsHeapBytes: 50, gcEvents: 2, vramBytes: 10, nativeHeapBytes: 10 }],
    ...overrides,
  };
}

// Allocation fault signature: trace event carrying byte evidence for the
// fault subsystem (args.bytesPerFrame >= fault.bytesPerFrame).
function exportedWithAllocation({ subsystem = 'engine', bytesPerFrame = 2048, sessionId = 's1' } = {}) {
  return {
    trace: {
      format: 'perfetto-json-v1',
      events: [{ name: 'memory', subsystem, args: { bytesPerFrame } }],
    },
    manifest: { device: 'test-device', os: 'test-os-1.0', engineVersion: '0.0.0-test', sessionId },
  };
}

// Render-pass fault signature: trace event named passName for the subsystem.
function exportedWithPass({ passName = 'shadow-blur', subsystem = 'gpu', costMs = 40, sessionId = 's1' } = {}) {
  return {
    trace: {
      format: 'perfetto-json-v1',
      events: [{ name: passName, subsystem, costMs }],
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

  it('round-trips pass name/subsystem/costMs into the dag', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(
      session('s1', {
        frames: [
          { passes: [{ name: 'shadow-blur', subsystem: 'gpu', costMs: 40 }] },
        ],
      }),
    );
    const node = out.dag.nodes.find((n) => n.label === 'shadow-blur');
    assert.ok(node, 'dag must carry the pass name');
    assert.equal(node.subsystem, 'gpu');
    assert.equal(node.costMs, 40);
  });

  it('round-trips pass costMs into vulkanTimings totals', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(
      session('s1', {
        frames: [
          { passes: [{ name: 'a', subsystem: 'engine', costMs: 10 }, { name: 'b', subsystem: 'gpu', costMs: 14 }] },
        ],
      }),
    );
    assert.equal(out.vulkanTimings.length, 1);
    assert.equal(out.vulkanTimings[0].totalMs, 24);
    assert.equal(out.vulkanTimings[0].passCount, 2);
  });

  it('forwards T1 layoutNodes into the layoutTree', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(
      session('s1', {
        layoutNodes: [
          { id: 'root-view', label: 'RootView', subsystem: 'platform' },
          { id: 'child-text', label: 'Text', subsystem: 'engine' },
        ],
      }),
    );
    const ids = out.layoutTree.root.children.map((c) => c.id);
    assert.ok(ids.includes('root-view'), 'layoutTree must forward T1 node ids');
    assert.ok(ids.includes('child-text'), 'layoutTree must forward T1 node ids');
    const child = out.layoutTree.root.children.find((c) => c.id === 'child-text');
    assert.equal(child.label, 'Text');
    assert.equal(child.subsystem, 'engine');
  });

  it('forwards T2 signalHistory into the signalGraph', () => {
    const { attachRetainedViews } = load();
    const out = attachRetainedViews(
      session('s1', {
        signalHistory: [
          { seq: 1, value: 'a' },
          { seq: 2, value: 'b' },
        ],
      }),
    );
    const ids = out.signalGraph.nodes.map((n) => n.id);
    assert.ok(ids.includes('1'), 'signalGraph must forward T2 seq ids');
    assert.ok(ids.includes('2'), 'signalGraph must forward T2 seq ids');
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
    const out = verifyAttribution(exportedWithAllocation({ subsystem: 'engine', bytesPerFrame: 2048 }), fault);
    assert.equal(out.attributedTo, 'engine');
  });

  it('attributes an injected extra render pass to its subsystem', () => {
    const { verifyAttribution } = load();
    const fault = { kind: 'render-pass', passName: 'shadow-blur', subsystem: 'gpu', costMs: 40 };
    const out = verifyAttribution(exportedWithPass({ passName: 'shadow-blur', subsystem: 'gpu', costMs: 40 }), fault);
    assert.equal(out.attributedTo, 'gpu');
  });

  it('distinguishes the two injected faults (allocation vs render pass)', () => {
    const { verifyAttribution } = load();
    const alloc = verifyAttribution(
      exportedWithAllocation({ subsystem: 'platform', bytesPerFrame: 2048 }),
      { kind: 'allocation', subsystem: 'platform', bytesPerFrame: 512 },
    );
    const pass = verifyAttribution(
      exportedWithPass({ passName: 'blur', subsystem: 'js', costMs: 30 }),
      { kind: 'render-pass', passName: 'blur', subsystem: 'js', costMs: 30 },
    );
    assert.equal(alloc.attributedTo, 'platform');
    assert.equal(pass.attributedTo, 'js');
    assert.notEqual(alloc.attributedTo, pass.attributedTo);
  });

  it('throws E_PROFILER_ATTRIBUTION_MISMATCH when the export carries no fault evidence', () => {
    const { verifyAttribution } = load();
    const clean = {
      trace: { format: 'perfetto-json-v1', events: [{ name: 'render', subsystem: 'engine', costMs: 4 }] },
      manifest: { device: 'test-device', os: 'test-os-1.0', engineVersion: '0.0.0-test', sessionId: 's1' },
    };
    assert.throws(
      () => verifyAttribution(clean, { kind: 'allocation', subsystem: 'engine', bytesPerFrame: 1024 }),
      (err) => {
        assert.equal(err.code, 'E_PROFILER_ATTRIBUTION_MISMATCH');
        return assertNoLeak(err);
      },
    );
    assert.throws(
      () => verifyAttribution(clean, { kind: 'render-pass', passName: 'shadow-blur', subsystem: 'gpu', costMs: 40 }),
      (err) => {
        assert.equal(err.code, 'E_PROFILER_ATTRIBUTION_MISMATCH');
        return assertNoLeak(err);
      },
    );
  });

  it('throws E_PROFILER_ATTRIBUTION_MISMATCH on subsystem mismatch despite a matching name', () => {
    const { verifyAttribution } = load();
    assert.throws(
      () =>
        verifyAttribution(
          exportedWithPass({ passName: 'shadow-blur', subsystem: 'gpu', costMs: 40 }),
          { kind: 'render-pass', passName: 'shadow-blur', subsystem: 'js', costMs: 40 },
        ),
      (err) => err.code === 'E_PROFILER_ATTRIBUTION_MISMATCH',
    );
  });

  it('rejects a malformed fault without leaking internals (ASSUMPTION)', () => {
    const { verifyAttribution } = load();
    for (const bad of [null, undefined, {}, { kind: 'allocation' }, { kind: 'nope', subsystem: 'engine' }]) {
      assert.throws(() => verifyAttribution(exportedWithAllocation(), bad), assertNoLeak);
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
