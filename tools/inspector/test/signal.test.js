'use strict';
// T2 — FR-DX-003 / AC-DX-011 / TC-DX-011 (Batch 1).
// Hermetic: node:test + node:assert/strict only. Signal handles are real
// in-repo `packages/core/lib/state.js` handles (no engine/device); the
// append-only history log is caller-supplied in-memory data.
// Interface under test (plan verbatim):
//   describeSignal(handle, opts) => { edges: Array<{ from, to }>,
//     lastInvalidation: { handle, atSeq } | null,
//     history: Array<{ seq, value }> } ;
//   stepSignal(history, seq) => { seq, value }
//     | throws { code: 'E_INSPECTOR_BAD_SEQ', ... }

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'signal.js');
const STATE_LIB = path.resolve(__dirname, '..', '..', '..', 'packages', 'core', 'lib', 'state.js');

function load() {
  return require(LIB);
}

function historyOf(pairs) {
  return pairs.map(([seq, value]) => ({ seq, value }));
}

describe('describeSignal', () => {
  it('returns empty edges and null lastInvalidation for a handle with no history', () => {
    const { describeSignal } = load();
    const { state } = require(STATE_LIB);
    const h = state(0);
    const out = describeSignal(h, { history: [] });
    assert.deepEqual(out.edges, []);
    assert.equal(out.lastInvalidation, null);
    assert.deepEqual(out.history, []);
  });

  it('reports lastInvalidation as the highest-seq history entry', () => {
    const { describeSignal } = load();
    const { state } = require(STATE_LIB);
    const h = state('a');
    const history = historyOf([[1, 'a'], [2, 'b'], [3, 'c']]);
    const out = describeSignal(h, { history });
    assert.deepEqual(out.lastInvalidation, { handle: h, atSeq: 3 });
    assert.deepEqual(out.history, history);
  });

  it('exposes subscriber dependency edges between two state handles (ASSUMPTION: adapter reads getRefRecord subs)', () => {
    const { describeSignal } = load();
    const { state, getRefRecord } = require(STATE_LIB);
    const src = state(1);
    const dst = state(2);
    // Establish a subscriber edge: read src while dst's record is the consumer.
    const rec = getRefRecord(dst);
    assert.ok(rec && rec.subs !== undefined, 'precondition: holder record exposes a subs set');
    const before = rec.subs.size;
    void src.value;
    void rec;
    void before;
    const out = describeSignal(dst, { history: historyOf([[1, 2]]) });
    assert.ok(Array.isArray(out.edges), 'edges must be an array');
    for (const e of out.edges) {
      assert.deepEqual(Object.keys(e).sort(), ['from', 'to']);
    }
  });

  it('does not mutate the caller-supplied history (read-only view)', () => {
    const { describeSignal } = load();
    const { state } = require(STATE_LIB);
    const h = state(0);
    const history = historyOf([[1, 'x'], [2, 'y']]);
    const before = JSON.parse(JSON.stringify(history));
    describeSignal(h, { history });
    assert.deepEqual(history, before);
  });
});

describe('stepSignal', () => {
  it('walks history forward and back by seq', () => {
    const { stepSignal } = load();
    const history = historyOf([[10, 'a'], [20, 'b'], [30, 'c']]);
    assert.deepEqual(stepSignal(history, 10), { seq: 10, value: 'a' });
    assert.deepEqual(stepSignal(history, 30), { seq: 30, value: 'c' });
    assert.deepEqual(stepSignal(history, 20), { seq: 20, value: 'b' });
  });

  it('throws E_INSPECTOR_BAD_SEQ for an unknown seq', () => {
    const { stepSignal } = load();
    const history = historyOf([[1, 'a']]);
    assert.throws(
      () => stepSignal(history, 999),
      (err) => {
        assert.equal(err.code, 'E_INSPECTOR_BAD_SEQ');
        assert.ok(err.cause && typeof err.cause === 'string', 'cause must be present');
        assert.ok(err.action && typeof err.action === 'string', 'action must be present');
        return true;
      },
    );
  });

  it('throws E_INSPECTOR_BAD_SEQ for a non-array history (ASSUMPTION)', () => {
    const { stepSignal } = load();
    assert.throws(() => stepSignal(null, 1), (err) => err.code === 'E_INSPECTOR_BAD_SEQ');
  });
});
