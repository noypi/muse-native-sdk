/**
 * TC-M3-012 — Batch 2 / T3: caret/selection model (FR-M3-006, AC-M3-012,
 * RULE-M3-004).
 *
 * Selection survives value updates, is settable by touch/keyboard/API
 * origins, and is reported through the selection binding
 * (`handle.selection` + `onSelectionChange`). Out-of-range inputs clamp to
 * `[0, text.length]`; `start > end` normalises; selection setters never
 * throw.
 *
 * Hermetic: node:test + node:assert/strict only. Collaborators (editing
 * engine, platform clipboard, inset frames, two-way binding, selection
 * callback) are in-memory fakes injected through `createTextInput`
 * constructor deps. No network, no secrets, no services, no engine imports.
 *
 * ASSUMPTION (origins): at this adapter surface touch/keyboard/API origins
 * are indistinguishable — every origin funnels through the same
 * `handle.setSelection(start, end)` entry and must produce an identical
 * `onSelectionChange` payload shape plus an identical `handle.selection`
 * binding read. The platform drivers that call it are owned by the TEXT
 * bridge (FR-TEXT-008) per RULE-M3-004.
 * ASSUMPTION (callback transport): `onSelectionChange` travels in the
 * `createTextInput` deps alongside `binding`, because the T3 `Interface:`
 * line names it a prop but the handle is constructed from deps only; the
 * coder reconciles this if the prop flows through the TSX descriptor
 * instead (GR-08 T1.10, not silently followed).
 * ASSUMPTION (read-through): an engine-side (platform) selection write is
 * observable via `getSelection()`/`handle.selection` on read; push
 * notification transport for engine-owned writes stays with the TEXT
 * bridge per RULE-M3-004.
 * ASSUMPTION A4 (DI): engines are injected as factory parameters, never
 * imported from engine paths.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { createTextInput } = require('../textinput.js');

// ---------------------------------------------------------------------------
// Fakes (in-memory, injected through constructor deps only)
// ---------------------------------------------------------------------------

function fakeEditingEngine() {
  const calls = [];
  let text = '';
  let selection = { start: 0, end: 0 };
  return {
    calls,
    setComposingText(s) {
      calls.push(['setComposingText', s]);
    },
    commitText(s) {
      calls.push(['commitText', s]);
      text = s;
    },
    cancelComposition() {
      calls.push(['cancelComposition']);
    },
    getText() {
      return text;
    },
    setSelection(start, end) {
      calls.push(['setSelection', start, end]);
      selection = { start, end };
    },
    getSelection() {
      return { ...selection };
    },
  };
}

function fakeClipboard({ text = '' } = {}) {
  const writes = [];
  return {
    writes,
    readText() {
      return text;
    },
    writeText(s) {
      writes.push(s);
    },
    hasText() {
      return text.length > 0;
    },
  };
}

function fakeInsets() {
  return {
    current() {
      return { imeBottom: 0 };
    },
    subscribe() {
      return () => {};
    },
  };
}

function fakeBinding(initial = '') {
  let current = initial;
  const pushes = [];
  return {
    pushes,
    get value() {
      return current;
    },
    set value(v) {
      current = v;
      pushes.push(v);
    },
  };
}

function makeDeps(overrides = {}) {
  const seen = [];
  return {
    editingEngine: fakeEditingEngine(),
    clipboard: fakeClipboard(),
    insets: fakeInsets(),
    onSelectionChange(sel) {
      seen.push({ ...sel });
    },
    ...overrides,
    get _seen() {
      return seen;
    },
  };
}

function selectHandle(text, overrides = {}) {
  const deps = makeDeps(overrides);
  const handle = createTextInput(deps);
  handle.commitText(text);
  deps.editingEngine.calls.length = 0;
  const seen = deps._seen;
  seen.length = 0;
  return { deps, handle, seen };
}

// ---------------------------------------------------------------------------
// AC-M3-012: selection survives value updates
// ---------------------------------------------------------------------------

test('selection survives a value update that keeps the text valid', () => {
  const { handle } = selectHandle('hello world');
  handle.setSelection(2, 5);
  assert.deepEqual(handle.getSelection(), { start: 2, end: 5 });
  handle.commitText('hello WORLD');
  assert.equal(handle.value, 'hello WORLD', 'value update must land');
  assert.deepEqual(
    handle.getSelection(),
    { start: 2, end: 5 },
    'selection must still read (2,5) after the value update',
  );
  assert.deepEqual(
    handle.selection,
    { start: 2, end: 5 },
    'live selection binding must still read (2,5) after the value update',
  );
});

test('programmatic binding-side value push preserves the stored selection', () => {
  const binding = fakeBinding('');
  const { handle } = selectHandle('hello world', { binding });
  handle.setSelection(2, 5);
  handle.commitText('hello WORLD');
  assert.equal(binding.value, 'hello WORLD', 'bound value must follow the push');
  assert.deepEqual(handle.getSelection(), { start: 2, end: 5 });
});

// ---------------------------------------------------------------------------
// AC-M3-012: settable by touch/keyboard/API with identical report shape
// ---------------------------------------------------------------------------

test('touch origin (collapsed tap) reports identical onSelectionChange + binding shape', () => {
  const { handle, seen } = selectHandle('hello world');
  handle.setSelection(4, 4);
  assert.deepEqual(seen, [{ start: 4, end: 4 }], 'tap must fire one onSelectionChange');
  assert.deepEqual(
    handle.selection,
    { start: 4, end: 4 },
    'selection binding must match the callback payload shape',
  );
  assert.deepEqual(handle.getSelection(), seen[0]);
});

test('keyboard origin (shift-selected range) reports identical onSelectionChange + binding shape', () => {
  const { handle, seen } = selectHandle('hello world');
  handle.setSelection(2, 5);
  assert.deepEqual(seen, [{ start: 2, end: 5 }], 'keyboard range must fire one onSelectionChange');
  assert.deepEqual(handle.selection, { start: 2, end: 5 });
  assert.deepEqual(handle.getSelection(), seen[0]);
});

test('API origin (reversed range) normalises and reports identical onSelectionChange + binding shape', () => {
  const { handle, seen } = selectHandle('hello world');
  handle.setSelection(5, 2);
  assert.deepEqual(seen, [{ start: 2, end: 5 }], 'API range must normalise then fire identically');
  assert.deepEqual(handle.selection, { start: 2, end: 5 });
  assert.deepEqual(handle.getSelection(), seen[0]);
});

test('every selection change reaches the injected editing engine', () => {
  const { deps, handle } = selectHandle('hello world');
  handle.setSelection(1, 3);
  assert.ok(
    deps.editingEngine.calls.some(([name, a, b]) => name === 'setSelection' && a === 1 && b === 3),
    'fake engine must receive setSelection(1, 3)',
  );
});

test('platform-side engine selection write is observable through getSelection and the binding', () => {
  const { deps, handle } = selectHandle('hello world');
  deps.editingEngine.setSelection(1, 3);
  assert.deepEqual(handle.getSelection(), { start: 1, end: 3 });
  assert.deepEqual(handle.selection, { start: 1, end: 3 });
});

// ---------------------------------------------------------------------------
// Boundary / error cases: clamp, normalise, never throws
// ---------------------------------------------------------------------------

test('out-of-range selection clamps to [0, text.length] instead of throwing', () => {
  const { handle, seen } = selectHandle('hello');
  assert.doesNotThrow(() => handle.setSelection(-5, 10000));
  assert.deepEqual(handle.getSelection(), { start: 0, end: 5 });
  assert.deepEqual(handle.selection, { start: 0, end: 5 });
  assert.deepEqual(seen.at(-1), { start: 0, end: 5 }, 'callback must carry the clamped range');
});

test('start > end normalises to (end, start)', () => {
  const { handle } = selectHandle('hello world');
  assert.doesNotThrow(() => handle.setSelection(9, 3));
  assert.deepEqual(handle.getSelection(), { start: 3, end: 9 });
});

test('collapsed caret (empty selection) is a valid selection that survives reads', () => {
  const { handle, seen } = selectHandle('hello');
  handle.setSelection(3, 3);
  assert.deepEqual(handle.getSelection(), { start: 3, end: 3 });
  assert.deepEqual(handle.selection, { start: 3, end: 3 });
  assert.deepEqual(seen.at(-1), { start: 3, end: 3 });
});

test('selection on empty text clamps to (0,0) and never throws', () => {
  const { handle } = selectHandle('');
  assert.doesNotThrow(() => handle.setSelection(3, 9));
  assert.deepEqual(handle.getSelection(), { start: 0, end: 0 });
});

test('non-finite selection inputs never throw and yield a valid range', () => {
  const { handle } = selectHandle('hello');
  assert.doesNotThrow(() => handle.setSelection(NaN, Infinity));
  const sel = handle.getSelection();
  assert.ok(
    Number.isInteger(sel.start) && Number.isInteger(sel.end),
    'selection must stay integer-valued',
  );
  assert.ok(sel.start >= 0 && sel.end >= 0 && sel.start <= 5 && sel.end <= 5);
});

test('setSelection without an onSelectionChange callback never throws', () => {
  const deps = makeDeps({ onSelectionChange: undefined });
  const handle = createTextInput(deps);
  handle.commitText('hello');
  assert.doesNotThrow(() => handle.setSelection(1, 2));
  assert.deepEqual(handle.getSelection(), { start: 1, end: 2 });
});
