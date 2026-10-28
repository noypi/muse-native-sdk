'use strict';
// T7 — FR-DX-004 / AC-DX-016 / TC-DX-016 (Batch 3).
// Hermetic: node:test + node:assert/strict only. The classifier is pure:
// snapshots are plain objects built inline — no device, no allocator,
// no GC, no live heap, no network.
// Interface under test (plan verbatim):
//   classifyLeak(snapshot) => { deltas, classification: 'js' | 'engine' | 'gpu' | 'platform', topGrower }
//   where snapshot = { engineBytes, jsHeapBytes, gcEvents, vramBytes, nativeHeapBytes }
// Per plan Detail the largest-growing arena beyond a caller-supplied
// threshold wins; GC-event presence is reported alongside but does not
// override arena math; tiebreak order is jsHeap > engine > vram > native.
// ASSUMPTION (recorded in T7.md): a single-point snapshot is classified by
// dominant-arena magnitude (deltas = per-arena growth from a zero baseline);
// the caller-supplied threshold is an internal default and the fixtures use
// clearly-dominant margins so the threshold value does not decide them.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'memory.js');

function load() {
  return require(LIB);
}

function snapshot(overrides) {
  return {
    engineBytes: 10 << 20,
    jsHeapBytes: 10 << 20,
    gcEvents: [],
    vramBytes: 10 << 20,
    nativeHeapBytes: 10 << 20,
    ...overrides,
  };
}

// topGrower names the winning arena; accept either the classification
// vocabulary ('js') or the snapshot field vocabulary ('jsHeapBytes').
const GROWER_WORDS = {
  js: ['js', 'jsheapbytes'],
  engine: ['engine', 'enginebytes'],
  gpu: ['gpu', 'vram', 'vrambytes'],
  platform: ['platform', 'native', 'nativeheapbytes'],
};

function assertTopGrower(out, expected) {
  assert.ok(out && typeof out.topGrower === 'string' && out.topGrower.length > 0, 'topGrower must be a non-empty string');
  const words = GROWER_WORDS[expected];
  assert.ok(
    words.some((w) => out.topGrower.toLowerCase().includes(w)),
    `topGrower ${JSON.stringify(out.topGrower)} must identify the ${expected} arena`,
  );
}

function assertClassification(out, expected) {
  assert.ok(out && typeof out.deltas === 'object' && out.deltas !== null, 'must return { deltas, ... }');
  assert.equal(out.classification, expected);
  assertTopGrower(out, expected);
}

describe('classifyLeak', () => {
  it("classifies a JS-heap-dominated snapshot with GC events as 'js'", () => {
    const { classifyLeak } = load();
    const out = classifyLeak(
      snapshot({
        jsHeapBytes: 180 << 20,
        gcEvents: [
          { type: 'major-gc', atMs: 100, reclaimedBytes: 2 << 20 },
          { type: 'major-gc', atMs: 250, reclaimedBytes: 1 << 20 },
        ],
      }),
    );
    assertClassification(out, 'js');
  });

  it("classifies an engine-allocator-dominated snapshot as 'engine'", () => {
    const { classifyLeak } = load();
    const out = classifyLeak(snapshot({ engineBytes: 220 << 20 }));
    assertClassification(out, 'engine');
  });

  it("classifies a VRAM-dominated snapshot as 'gpu'", () => {
    const { classifyLeak } = load();
    const out = classifyLeak(snapshot({ vramBytes: 310 << 20 }));
    assertClassification(out, 'gpu');
  });

  it("classifies a native-heap-dominated snapshot as 'platform'", () => {
    const { classifyLeak } = load();
    const out = classifyLeak(snapshot({ nativeHeapBytes: 160 << 20 }));
    assertClassification(out, 'platform');
  });

  it('does not let GC-event presence override arena math (engine still wins)', () => {
    const { classifyLeak } = load();
    const out = classifyLeak(
      snapshot({
        engineBytes: 220 << 20,
        jsHeapBytes: 12 << 20,
        gcEvents: [{ type: 'major-gc', atMs: 100, reclaimedBytes: 1 << 20 }],
      }),
    );
    assertClassification(out, 'engine');
  });

  it('breaks exact ties jsHeap > engine > vram > native (ASSUMPTION)', () => {
    const { classifyLeak } = load();
    const tied = snapshot({
      engineBytes: 50 << 20,
      jsHeapBytes: 50 << 20,
      vramBytes: 50 << 20,
      nativeHeapBytes: 50 << 20,
    });
    assert.equal(classifyLeak(tied).classification, 'js');
    const { jsHeapBytes: _drop, ...rest } = tied;
    void _drop;
    assert.equal(classifyLeak({ ...rest, jsHeapBytes: 1 << 20 }).classification, 'engine');
  });

  it('rejects a malformed snapshot without leaking internals (ASSUMPTION)', () => {
    const { classifyLeak } = load();
    for (const bad of [null, undefined, {}, { jsHeapBytes: 'huge' }, { engineBytes: -1 }]) {
      assert.throws(
        () => classifyLeak(bad),
        (err) => {
          assert.ok(err instanceof Error, 'must throw an Error');
          assert.doesNotMatch(String(err.stack ?? '').split('\n').slice(1).join('\n'), /node_modules/);
          return true;
        },
      );
    }
  });
});
