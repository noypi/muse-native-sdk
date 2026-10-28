'use strict';
// T6 — FR-DX-004 / AC-DX-015 / TC-DX-015 (Batch 3).
// Hermetic: node:test + node:assert/strict only. The session log is an
// in-memory structure inside the unit under test; frames are plain objects
// built inline — no device, no Perfetto binary, no network, no clock.
// Interface under test (plan verbatim):
//   recordFrame(frame) => { id }
//   attributeJank(sessionId, budgetMs) => Array<{ frameId, slowPass, subsystem, overByMs }>
// Frame shape under test (per plan Detail):
//   { sessionId, passes: [{ name, subsystem, costMs }] }
// where `subsystem` is a verbatim caller label (engine/js/gpu/platform).
// Isolation: the lib holds a module-global frame log, so every test clears
// it first via clearFrames()/resetTimeline() — no ordering dependence.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'frame-timeline.js');

function load() {
  return require(LIB);
}

function frame(sessionId, passes) {
  return { sessionId, passes };
}

function pass(name, subsystem, costMs) {
  return { name, subsystem, costMs };
}

beforeEach(() => {
  const lib = load();
  (lib.clearFrames ?? lib.resetTimeline)();
});

describe('recordFrame', () => {
  it('records a frame and returns an id', () => {
    const { recordFrame } = load();
    const out = recordFrame(frame('s1', [pass('render', 'engine', 4)]));
    assert.ok(out && typeof out.id === 'string' && out.id.length > 0, 'must return { id }');
  });

  it('returns distinct ids for distinct frames', () => {
    const { recordFrame } = load();
    const a = recordFrame(frame('s1', [pass('render', 'engine', 4)]));
    const b = recordFrame(frame('s1', [pass('render', 'engine', 4)]));
    assert.notEqual(a.id, b.id);
  });

  it('rejects a malformed frame with E_PROFILER_MALFORMED_FRAME without leaking internals', () => {
    const { recordFrame } = load();
    for (const bad of [null, undefined, {}, { sessionId: 's1' }, { sessionId: 's1', passes: null }]) {
      assert.throws(
        () => recordFrame(bad),
        (err) => {
          assert.ok(err instanceof Error, 'must throw an Error');
          assert.equal(err.code, 'E_PROFILER_MALFORMED_FRAME');
          assert.doesNotMatch(String(err.stack ?? '').split('\n').slice(1).join('\n'), /node_modules/);
          return true;
        },
      );
    }
  });

  it('rejects a bad budgetMs with E_PROFILER_MALFORMED_FRAME', () => {
    const { recordFrame, attributeJank } = load();
    recordFrame(frame('s1', [pass('render', 'engine', 24)]));
    for (const badBudget of [null, '16', -1, NaN, Infinity]) {
      assert.throws(() => attributeJank('s1', badBudget), (err) => err.code === 'E_PROFILER_MALFORMED_FRAME');
    }
  });

  it('clearFrames()/resetTimeline() isolate runs (alias resets the log)', () => {
    const lib = load();
    lib.recordFrame(frame('s1', [pass('render', 'engine', 99)]));
    assert.equal(lib.attributeJank('s1', 16).length, 1);
    const reset = lib.resetTimeline ?? lib.clearFrames;
    reset();
    assert.deepEqual(lib.attributeJank('s1', 16), []);
  });
});

describe('attributeJank', () => {
  it('attributes a slow render pass to that pass and subsystem with overByMs > 0', () => {
    const { recordFrame, attributeJank } = load();
    const { id } = recordFrame(frame('s1', [pass('render', 'engine', 24)]));
    const out = attributeJank('s1', 16);
    assert.equal(out.length, 1);
    assert.equal(out[0].frameId, id);
    assert.equal(out[0].slowPass, 'render');
    assert.equal(out[0].subsystem, 'engine');
    assert.equal(out[0].overByMs, 8);
    assert.ok(out[0].overByMs > 0);
  });

  it('passes the caller subsystem label through verbatim', () => {
    const { recordFrame, attributeJank } = load();
    recordFrame(frame('s1', [pass('composite', 'gpu', 30)]));
    const out = attributeJank('s1', 16);
    assert.equal(out.length, 1);
    assert.equal(out[0].subsystem, 'gpu');
    assert.equal(out[0].slowPass, 'composite');
  });

  it('leaves clean frames unattributed', () => {
    const { recordFrame, attributeJank } = load();
    recordFrame(frame('s1', [pass('render', 'engine', 4), pass('js', 'js', 3)]));
    recordFrame(frame('s1', [pass('render', 'engine', 8)]));
    assert.deepEqual(attributeJank('s1', 16), []);
  });

  it('does not attribute a frame exactly at budget (overByMs must be > 0)', () => {
    const { recordFrame, attributeJank } = load();
    recordFrame(frame('s1', [pass('render', 'engine', 16)]));
    assert.deepEqual(attributeJank('s1', 16), []);
  });

  it('attributes only the slowest over-budget pass in a frame (ASSUMPTION)', () => {
    const { recordFrame, attributeJank } = load();
    const { id } = recordFrame(
      frame('s1', [pass('js-layout', 'js', 20), pass('render', 'engine', 50), pass('flip', 'platform', 5)]),
    );
    const out = attributeJank('s1', 16);
    assert.equal(out.length, 1);
    assert.equal(out[0].frameId, id);
    assert.equal(out[0].slowPass, 'render');
    assert.equal(out[0].subsystem, 'engine');
    assert.equal(out[0].overByMs, 34);
  });

  it('is a pure scan: repeated polls return the same jank until clearFrames()', () => {
    const { recordFrame, attributeJank, clearFrames } = load();
    const { id } = recordFrame(frame('s1', [pass('render', 'engine', 24)]));
    const first = attributeJank('s1', 16);
    const second = attributeJank('s1', 16);
    assert.equal(first.length, 1);
    assert.deepEqual(second, first);
    assert.equal(second[0].frameId, id);
    clearFrames();
    assert.deepEqual(attributeJank('s1', 16), []);
  });

  it('isolates sessions: another session frames are not attributed', () => {
    const { recordFrame, attributeJank } = load();
    recordFrame(frame('other', [pass('render', 'engine', 99)]));
    recordFrame(frame('s1', [pass('render', 'engine', 4)]));
    assert.deepEqual(attributeJank('s1', 16), []);
  });

  it('returns an empty array for an unknown session (ASSUMPTION)', () => {
    const { attributeJank } = load();
    assert.deepEqual(attributeJank('no-such-session', 16), []);
  });
});
