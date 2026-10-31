// Hermetic: in-memory fakes only — no device/adb/network. Device leg: RunInsetLeg on 127.0.0.1:6569 (TC-M3-013).
'use strict';
// T4 — Follow the animated IME inset: stay above the keyboard every frame,
// no jump, no full-tree relayout (Batch 3, FR-M3-006 AC-M3-013, TC-M3-013).
//
// Plan Interface (verbatim):
// `handle.followInsets(): () => void` — subscribes to `deps.insets`,
// translates only the field (`handle.insetOffset: number` equals the latest
// `imeBottom` each frame), returns an unsubscribe function. No other node is
// touched: `relayoutSpy.treesRelayedOut() === 0` across a scripted open/close.
//
// Spec refs: FR-M3-006, AC-M3-013, TC-M3-013, FR-LAYOUT-012 (required).
// Detail: consumes FR-LAYOUT-012 inset-frame values as an opaque per-frame
// `{ imeBottom }` contract; applied as a translation offset on the field only
// (never a layout pass); unsubscribe disposes the subscription (no leaks
// across mount/unmount cycles).
//
// Hermetic: node:test + node:assert/strict only. Injected `InsetFrames` fake
// (`current`/`subscribe`); in-memory editing-engine + clipboard fakes; no
// network, no secrets, no config, no live services, no wall-clock timing.
//
// Geometry convention (ASSUMPTION A5, structural verification): screen height
// SCREEN_H; the field is docked (resting bottom edge FIELD_BOTTOM = SCREEN_H);
// the keyboard's top edge is keyboardTop = SCREEN_H - imeBottom; the follow
// offset lifts the field in keyboard-lift space, so the per-frame invariant is
// fieldBottom + insetOffset >= keyboardTop.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createTextInput } = require('../textinput.js');

const SCREEN_H = 800;
const FIELD_BOTTOM = 800; // docked resting bottom edge
const OPEN_CLOSE = [0, 120, 240, 336, 336, 240, 120, 0];

const keyboardTop = (imeBottom) => SCREEN_H - imeBottom;

function makeInsetFrames(initial = 0) {
  let current = initial;
  const listeners = new Set();
  return {
    fake: {
      current: () => ({ imeBottom: current }),
      subscribe: (fn) => {
        listeners.add(fn);
        return () => {
          listeners.delete(fn);
        };
      },
    },
    emit(imeBottom) {
      current = imeBottom;
      for (const fn of [...listeners]) fn({ imeBottom });
    },
    subscriberCount: () => listeners.size,
  };
}

function makeEditingEngine() {
  const calls = [];
  let text = 'hello';
  let selection = { start: 0, end: 0 };
  return {
    calls,
    fake: {
      setComposingText: (s) => {
        calls.push(['setComposingText', s]);
      },
      commitText: (s) => {
        calls.push(['commitText', s]);
        text = s;
      },
      cancelComposition: () => {
        calls.push(['cancelComposition']);
      },
      getText: () => text,
      setSelection: (start, end) => {
        calls.push(['setSelection', start, end]);
        selection = { start, end };
      },
      getSelection: () => ({ ...selection }),
    },
  };
}

function makeClipboard() {
  return {
    readText: () => '',
    writeText: () => {},
    hasText: () => false,
  };
}

// Structural relayout spy (ASSUMPTION A5): the hermetic unit exposes no tree,
// so a full-tree pass could only travel through the injected collaborators —
// the handle's sole channels to anything outside the field. Any collaborator
// call while inset frames are delivered counts as a tree-affecting pass.
function makeRelayoutSpy(engineCalls) {
  let baseline = engineCalls.length;
  return {
    arm() {
      baseline = engineCalls.length;
    },
    treesRelayedOut() {
      return engineCalls.length - baseline;
    },
  };
}

function setup(initialInset = 0) {
  const insets = makeInsetFrames(initialInset);
  const engine = makeEditingEngine();
  const handle = createTextInput({
    editingEngine: engine.fake,
    clipboard: makeClipboard(),
    insets: insets.fake,
  });
  return { handle, insets, engine };
}

describe('T4 — followInsets tracks the animated IME inset (AC-M3-013)', () => {
  it('followInsets returns an unsubscribe function and insetOffset starts at the current imeBottom', () => {
    const { handle, insets } = setup(0);
    const unsub = handle.followInsets();
    try {
      assert.equal(typeof unsub, 'function');
      assert.equal(handle.insetOffset, insets.fake.current().imeBottom);
    } finally {
      unsub();
    }
  });

  it('insetOffset equals the latest imeBottom on every open frame (0→120→240→336)', () => {
    const { handle, insets } = setup();
    const unsub = handle.followInsets();
    try {
      for (const ime of [0, 120, 240, 336]) {
        insets.emit(ime);
        assert.equal(handle.insetOffset, ime);
      }
    } finally {
      unsub();
    }
  });

  it('fieldBottom + insetOffset >= keyboardTop on every frame of a scripted open/close', () => {
    const { handle, insets } = setup();
    const unsub = handle.followInsets();
    try {
      for (const ime of OPEN_CLOSE) {
        insets.emit(ime);
        assert.ok(
          FIELD_BOTTOM + handle.insetOffset >= keyboardTop(ime),
          `frame imeBottom=${ime}: ${FIELD_BOTTOM} + ${handle.insetOffset} >= ${keyboardTop(ime)}`,
        );
      }
    } finally {
      unsub();
    }
  });

  it('offset deltas never exceed the frame inset delta and follow its direction (no jump)', () => {
    const { handle, insets } = setup();
    const unsub = handle.followInsets();
    try {
      const seen = [];
      for (const ime of OPEN_CLOSE) {
        insets.emit(ime);
        seen.push(handle.insetOffset);
      }
      for (let i = 1; i < OPEN_CLOSE.length; i += 1) {
        const dIme = OPEN_CLOSE[i] - OPEN_CLOSE[i - 1];
        const dOff = seen[i] - seen[i - 1];
        assert.ok(
          Math.abs(dOff) <= Math.abs(dIme),
          `frame ${i}: |${dOff}| <= |${dIme}|`,
        );
        assert.ok(
          Math.sign(dOff) === Math.sign(dIme),
          `frame ${i}: offset moves with the inset (${dOff} vs ${dIme})`,
        );
      }
      // Duplicate hold frame (336→336) is perfectly still — no jump.
      assert.equal(seen[4] - seen[3], 0);
    } finally {
      unsub();
    }
  });

  it('closing the keyboard returns the offset to rest without overshoot or negative values', () => {
    const { handle, insets } = setup();
    const unsub = handle.followInsets();
    try {
      for (const ime of OPEN_CLOSE) {
        insets.emit(ime);
        assert.ok(handle.insetOffset >= 0, `offset never negative (ime=${ime})`);
      }
      assert.equal(handle.insetOffset, 0);
    } finally {
      unsub();
    }
  });

  it('zero full-tree passes across the scripted open/close; value and selection untouched', () => {
    const { handle, insets, engine } = setup();
    const spy = makeRelayoutSpy(engine.calls);
    const valueBefore = handle.value;
    const selectionBefore = handle.getSelection();
    const unsub = handle.followInsets();
    try {
      spy.arm();
      for (const ime of OPEN_CLOSE) insets.emit(ime);
      assert.equal(spy.treesRelayedOut(), 0);
      assert.deepEqual(handle.getSelection(), selectionBefore);
      assert.equal(handle.value, valueBefore);
    } finally {
      unsub();
    }
  });

  it('unsubscribe disposes the subscription: later frames are ignored and nothing leaks', () => {
    const { handle, insets } = setup();
    const unsub = handle.followInsets();
    insets.emit(120);
    assert.equal(handle.insetOffset, 120);
    unsub();
    assert.equal(insets.subscriberCount(), 0);
    insets.emit(240);
    insets.emit(336);
    assert.equal(handle.insetOffset, 120);
    assert.doesNotThrow(() => unsub());
  });

  it('a later followInsets re-subscribes cleanly (mount/unmount cycle)', () => {
    const { handle, insets } = setup();
    const first = handle.followInsets();
    first();
    assert.equal(insets.subscriberCount(), 0);
    const second = handle.followInsets();
    try {
      assert.equal(insets.subscriberCount(), 1);
      insets.emit(200);
      assert.equal(handle.insetOffset, 200);
    } finally {
      second();
    }
    assert.equal(insets.subscriberCount(), 0);
  });
});
