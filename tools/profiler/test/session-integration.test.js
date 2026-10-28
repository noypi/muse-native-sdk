'use strict';
// Acceptance integration: FR-DX-003 + FR-DX-004 composed session flow.
// AC-DX-014 (node-pick) -> AC-DX-015 (frame capture) -> AC-DX-017 (export)
// -> AC-DX-019 (fault attribution). Tester-owned integration test; not a unit test.
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { pickNode } = require('../../inspector/lib/node-pick.js');
const timeline = require('../lib/frame-timeline.js');
const { exportSession } = require('../lib/export.js');
const { attachRetainedViews, verifyAttribution } = require('../lib/retained-views.js');

const DEVICE = { device: 'acceptance-fixture', os: 'test-os-1', engineVersion: '0.0-test', sessionId: 'acceptance-s1' };

describe('acceptance: composed inspector+profiler session flow', () => {
  it('pickNode -> frames -> export -> attribution round-trips in one session', () => {
    if (timeline.clearFrames) timeline.clearFrames();
    // AC-DX-014: node pick returns box + declaration + signal, no log calls.
    const layoutSource = {
      getConstraintInput: (id) => ({ id, flexDirection: 'row' }),
      getComputedBox: (id) => ({ x: 8, y: 16, width: 320, height: 48 }),
      getWinningDeclaration: (id) => ({ declaration: '.row{flex:1}', origin: 'app.css:1' }),
    };
    const picked = pickNode('acceptance-node', { layoutSource, signalOf: () => ({ handle: 'h1', atSeq: 7 }) });
    assert.equal(picked.nodeId, 'acceptance-node');
    assert.deepEqual(picked.computedBox, { x: 8, y: 16, width: 320, height: 48 });
    assert.ok(picked.lastInvalidation && picked.lastInvalidation.atSeq === 7);

    // AC-DX-015: capture a janky frame and attribute it.
    timeline.recordFrame({ sessionId: 'acceptance-s1', passes: [{ name: 'render', subsystem: 'engine', costMs: 30 }] });
    const jank = timeline.attributeJank('acceptance-s1', 16);
    assert.equal(jank.length, 1);
    assert.equal(jank[0].slowPass, 'render');
    assert.ok(jank[0].overByMs > 0);

    // AC-DX-018: retained views expose all four views for the same session.
    const views = attachRetainedViews({
      sessionId: 'acceptance-s1',
      layoutNodes: [{ nodeId: picked.nodeId, box: picked.computedBox }],
      signalHistory: [{ seq: 7, from: 'h1', to: 'acceptance-node' }],
      frames: [{ passes: [{ name: 'render', subsystem: 'engine', costMs: 30 }] }],
      snapshots: [],
    });
    assert.deepEqual(Object.keys(views).sort(), ['dag', 'layoutTree', 'signalGraph', 'vulkanTimings']);

    // AC-DX-017: export yields a parseable trace plus a complete manifest.
    const exported = exportSession(
      { sessionId: 'acceptance-s1', frames: [{ passes: [{ name: 'render', subsystem: 'engine', costMs: 30 }] }], snapshots: [] },
      DEVICE,
    );
    assert.equal(exported.trace.format, 'perfetto-json-v1');
    assert.ok(JSON.parse(JSON.stringify(exported.trace)));
    assert.deepEqual(exported.manifest, DEVICE);

    // AC-DX-019: an injected render-pass fault attributes to the right subsystem.
    const verdict = verifyAttribution(exported, { kind: 'render-pass', passName: 'render', subsystem: 'engine', costMs: 30 });
    assert.equal(verdict.attributedTo, 'engine');
  });
});
