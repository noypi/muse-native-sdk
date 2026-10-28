'use strict';
// T5 — Cut/copy/paste/select-all through the platform clipboard (Batch 4,
// FR-M3-006 AC-M3-014, TC-M3-014).
//
// Plan Interface (verbatim):
// `handle.cut(): void; handle.copy(): void; handle.paste(): void |
// Promise<void>; handle.selectAll(): void` with `contentType: "plain" |
// "rich"` prop — `copy`/`cut` write the selection (plain string for
// `"plain"`; `{ text, spans }` payload for `"rich"`) via
// `deps.clipboard.writeText`; `paste` reads via `deps.clipboard.readText`
// and inserts at the caret as an ordinary edit (bound value updates
// identically); `cut` additionally deletes the selection; empty selection
// makes `cut`/`copy` no-ops.
//
// Spec refs: FR-M3-006, AC-M3-014, TC-M3-014, RULE-M3-004,
// FR-TEXT-008 (required), FR-MCORE-007 (required).
//
// Hermetic: node:test + node:assert/strict only. Collaborators (editing
// engine, platform clipboard, inset frames, two-way binding) are in-memory
// fakes injected through `createTextInput` constructor deps. No network, no
// secrets, no config, no live services, no engine imports.
//
// ASSUMPTION (contentType channel): `contentType` arrives via
// `deps.contentType` (`"plain"` default), because the T5 `Interface:` line
// names it a prop but the handle is constructed from deps only (same
// rationale as the T3 `onSelectionChange` contract). The coder reconciles
// this if the prop flows through the TSX descriptor instead (GR-08 T1.10,
// not silently followed); the copy/cut payload shape per content type
// stands either way.
// ASSUMPTION (rich spans): span entries are opaque objects preserved by
// value — the tests assert deep-equality round-trip, never span schema.
// A pasted `{ text, spans }` payload inserts its `.text` through the
// ordinary commit path and the handle retains the spans so a later
// `copy`/`cut` reproduces them; under `"plain"` only the string travels.
// ASSUMPTION (writeText payload): under `"rich"`, `writeText` accepts the
// `{ text, spans }` object payload (the T1 `PlatformClipboard` line types
// `writeText(s: string)`; T5 overrides it for rich).
// ASSUMPTION A4 (DI): engines are injected as factory parameters, never
// imported from engine paths.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createTextInput } = require('../textinput.js');

const RICH_SPANS = [{ start: 0, end: 4, style: 'bold' }];

// ---------------------------------------------------------------------------
// Fakes (in-memory, injected through constructor deps only)
// ---------------------------------------------------------------------------

function makeEditingEngine() {
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

function makeClipboard(seed = '') {
  const writes = [];
  let stored = seed;
  return {
    writes,
    readText() {
      return stored;
    },
    writeText(s) {
      writes.push(s);
      stored = typeof s === 'string' ? s : s.text;
    },
    hasText() {
      return stored.length > 0;
    },
  };
}

function makeInsets() {
  return {
    current() {
      return { imeBottom: 0 };
    },
    subscribe() {
      return () => {};
    },
  };
}

function makeBinding(initial = '') {
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
  const binding = makeBinding('');
  return {
    editingEngine: makeEditingEngine(),
    clipboard: makeClipboard(''),
    insets: makeInsets(),
    binding,
    ...overrides,
  };
}

function seedText(deps, text) {
  const handle = createTextInput(deps);
  handle.commitText(text);
  deps.editingEngine.calls.length = 0;
  deps.binding.pushes.length = 0;
  return handle;
}

// ---------------------------------------------------------------------------
// AC-M3-014 plain: select-all → copy → delete → paste restores exact text
// ---------------------------------------------------------------------------

describe('plain select-all/copy/delete/paste round-trip', () => {
  it('selectAll selects the whole text', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.selectAll();
    assert.deepEqual(handle.getSelection(), { start: 0, end: 5 });
    assert.deepEqual(handle.selection, { start: 0, end: 5 });
  });

  it('copy writes the exact selected text as a plain string via the injected clipboard', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.selectAll();
    handle.copy();
    assert.deepEqual(deps.clipboard.writes, ['hello']);
    assert.equal(typeof deps.clipboard.writes[0], 'string');
  });

  it('select-all, copy, delete, paste restores the exact text and bound value', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.selectAll();
    handle.copy();
    handle.commitText('');
    assert.equal(handle.value, '');
    handle.paste();
    assert.equal(handle.value, 'hello', 'paste must restore the exact text');
    assert.equal(deps.binding.value, 'hello', 'bound value must follow the paste');
  });

  it('paste updates the bound value exactly like a typed edit', () => {
    const typedDeps = makeDeps();
    const typed = createTextInput(typedDeps);
    typed.commitText('hello');

    const pastedDeps = makeDeps({ clipboard: makeClipboard('hello') });
    const pasted = createTextInput(pastedDeps);
    pasted.paste();

    assert.deepEqual(pastedDeps.binding.pushes, typedDeps.binding.pushes);
    assert.equal(pasted.value, typed.value);
  });

  it('paste inserts at the caret, replacing the current selection', () => {
    const deps = makeDeps({ clipboard: makeClipboard('XYZ') });
    const handle = seedText(deps, 'hello');
    handle.setSelection(1, 4);
    handle.paste();
    assert.equal(handle.value, 'hXYZo');
    assert.equal(deps.binding.value, 'hXYZo');
    assert.deepEqual(handle.getSelection(), { start: 4, end: 4 });
  });
});

// ---------------------------------------------------------------------------
// AC-M3-014 rich: `{ text, spans }` round-trips without dropping spans
// ---------------------------------------------------------------------------

describe('rich content-type round-trip', () => {
  it('rich copy writes a { text, spans } payload preserving spans', () => {
    // Seed via a rich clipboard payload, then copy it back.
    const richDeps = makeDeps({
      contentType: 'rich',
      clipboard: makeClipboard({ text: 'rich payload', spans: RICH_SPANS }),
    });
    const rich = createTextInput(richDeps);
    rich.paste();
    assert.equal(rich.value, 'rich payload', 'rich paste inserts the text part');
    rich.selectAll();
    rich.copy();
    assert.equal(richDeps.clipboard.writes.length, 1);
    assert.deepEqual(richDeps.clipboard.writes[0], {
      text: 'rich payload',
      spans: RICH_SPANS,
    });
  });

  it('rich cut writes the { text, spans } payload and deletes the selection', () => {
    const deps = makeDeps({
      contentType: 'rich',
      clipboard: makeClipboard({ text: 'rich payload', spans: RICH_SPANS }),
    });
    const handle = createTextInput(deps);
    handle.paste();
    assert.equal(handle.value, 'rich payload');
    handle.selectAll();
    handle.cut();
    assert.deepEqual(deps.clipboard.writes.at(-1), {
      text: 'rich payload',
      spans: RICH_SPANS,
    });
    assert.equal(handle.value, '', 'cut must delete the selection');
    assert.equal(deps.binding.value, '', 'bound value must follow the cut');
  });

  it('plain mode carries only the string even for a rich clipboard payload', () => {
    const deps = makeDeps({
      clipboard: makeClipboard({ text: 'rich payload', spans: RICH_SPANS }),
    });
    const handle = createTextInput(deps);
    handle.paste();
    assert.equal(handle.value, 'rich payload');
    handle.selectAll();
    handle.copy();
    assert.deepEqual(deps.clipboard.writes, ['rich payload']);
  });
});

// ---------------------------------------------------------------------------
// Boundary / error cases: collapsed-caret no-ops, empty-clipboard no-op
// ---------------------------------------------------------------------------

describe('clipboard boundary and error cases', () => {
  it('copy with a collapsed caret changes nothing and writes nothing', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.setSelection(2, 2);
    handle.copy();
    assert.deepEqual(deps.clipboard.writes, []);
    assert.equal(handle.value, 'hello');
    assert.deepEqual(deps.binding.pushes, []);
  });

  it('cut with a collapsed caret changes nothing and writes nothing', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.setSelection(2, 2);
    handle.cut();
    assert.deepEqual(deps.clipboard.writes, []);
    assert.equal(handle.value, 'hello', 'text must be untouched');
    assert.equal(deps.binding.value, 'hello', 'bound value must be untouched');
    assert.deepEqual(handle.getSelection(), { start: 2, end: 2 });
  });

  it('cut with a selection deletes it, writes it, and collapses the caret', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.setSelection(1, 4);
    handle.cut();
    assert.deepEqual(deps.clipboard.writes, ['ell']);
    assert.equal(handle.value, 'ho');
    assert.equal(deps.binding.value, 'ho');
    assert.deepEqual(handle.getSelection(), { start: 1, end: 1 });
  });

  it('paste with an empty clipboard is a no-op', () => {
    const deps = makeDeps({ clipboard: makeClipboard('') });
    const handle = seedText(deps, 'hello');
    handle.setSelection(5, 5);
    handle.paste();
    assert.equal(handle.value, 'hello');
    assert.equal(deps.binding.value, 'hello');
    assert.deepEqual(deps.binding.pushes, []);
    assert.deepEqual(handle.getSelection(), { start: 5, end: 5 });
  });

  it('async clipboard read resolves before inserting as an ordinary edit', async () => {
    const deps = makeDeps();
    deps.clipboard = {
      writes: [],
      readText: () => Promise.resolve('hello'),
      writeText(s) {
        deps.clipboard.writes.push(s);
      },
      hasText: () => true,
    };
    const handle = createTextInput(deps);
    await handle.paste();
    assert.equal(handle.value, 'hello');
    assert.equal(deps.binding.value, 'hello');
  });
});

// ---------------------------------------------------------------------------
// RULE-M3-004 / CONFLICT-M3-003: the injected PlatformClipboard is the only
// transport — never a private buffer
// ---------------------------------------------------------------------------

describe('clipboard transport is the injected PlatformClipboard only', () => {
  it('copy is observable only on the injected clipboard fake', () => {
    const deps = makeDeps();
    const handle = seedText(deps, 'hello');
    handle.selectAll();
    handle.copy();
    assert.equal(deps.clipboard.writes.length, 1);
    assert.equal(deps.editingEngine.calls.filter(([n]) => n === 'commitText').length, 0);
  });

  it('handles do not share clipboard state: copy on A never leaks into B', () => {
    const depsA = makeDeps();
    const handleA = seedText(depsA, 'hello');
    handleA.selectAll();
    handleA.copy();

    const depsB = makeDeps({ clipboard: makeClipboard('') });
    const handleB = seedText(depsB, 'other');
    handleB.selectAll();
    handleB.paste();
    assert.equal(handleB.value, 'other', 'no private buffer may leak across handles');
    assert.equal(depsB.binding.value, 'other');
  });
});
