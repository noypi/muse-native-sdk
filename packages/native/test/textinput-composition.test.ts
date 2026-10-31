// Hermetic: in-memory fakes only — no device/adb/network. Device leg: RunCompositionLeg on 127.0.0.1:6569 (TC-M3-011).
/**
 * TC-M3-011 (contract shape) — Batch 1 / T1: the `TextInput` component
 * surface over the tier-1 text-field edit core (FR-M3-006, AC-M3-011
 * framing for composition, RULE-M3-004).
 *
 * Contract shape only: asserts the T1 surface — constructible from TSX
 * props per the manifest schema, injected fake engines receive every edit
 * call, no edit path touches engine internals directly.
 *
 * Hermetic: node:test + node:assert/strict only. Collaborators
 * (editing engine, platform clipboard, inset frames, two-way binding) are
 * in-memory fakes injected through `createTextInput` constructor deps.
 * Reads muse.manifest.json and index.d.ts from disk. No network, no
 * secrets, no services, no engine imports.
 *
 * ASSUMPTION A1 (pure JS surface): `packages/native` stays a pure JS +
 * types + JSON surface; "native component" AC language is satisfied
 * structurally via manifest `native` backing + descriptor + adapter
 * contracts (no C++ tree exists in this checkout).
 * ASSUMPTION A3 (additive manifest): T1 adds a fully-specified `TextInput`
 * entry and reuses the existing tier-1 `TextFieldEditCore` entry without
 * renaming existing exports.
 * ASSUMPTION A4 (DI): engines are injected as factory parameters, never
 * imported from engine paths.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { createTextInput } = require('../textinput.js');
const { components } = require('../index.js');
const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);
const DTS_URL = new URL('../index.d.ts', import.meta.url);

function loadManifest() {
  return JSON.parse(readFileSync(MANIFEST_URL, 'utf8'));
}

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
  return {
    editingEngine: fakeEditingEngine(),
    clipboard: fakeClipboard(),
    insets: fakeInsets(),
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// T1 surface: factory exists and constructs a handle
// ---------------------------------------------------------------------------

test('createTextInput is exported from the textinput module', () => {
  assert.equal(typeof createTextInput, 'function');
});

test('createTextInput constructs a TextInputHandle with the full method surface', () => {
  const handle = createTextInput(makeDeps());
  for (const method of [
    'setComposingText',
    'commitText',
    'cancelComposition',
    'setSelection',
    'getSelection',
    'cut',
    'copy',
    'paste',
    'selectAll',
    'dispose',
  ]) {
    assert.equal(typeof handle[method], 'function', `handle.${method} must be a function`);
  }
  assert.equal(typeof handle.value, 'string');
  assert.ok(
    typeof handle.selection === 'object' && handle.selection !== null,
    'handle.selection must be an object',
  );
});

// ---------------------------------------------------------------------------
// AC-M3-011 framing: injected fake engine receives every edit call
// ---------------------------------------------------------------------------

test('setComposingText delegates to the injected editing engine', () => {
  const deps = makeDeps();
  const handle = createTextInput(deps);
  handle.setComposingText('ni');
  assert.ok(
    deps.editingEngine.calls.some(([name, arg]) => name === 'setComposingText' && arg === 'ni'),
    'fake engine must receive setComposingText("ni")',
  );
});

test('commitText delegates to the injected editing engine', () => {
  const deps = makeDeps();
  const handle = createTextInput(deps);
  handle.commitText('nihao');
  assert.ok(
    deps.editingEngine.calls.some(([name, arg]) => name === 'commitText' && arg === 'nihao'),
    'fake engine must receive commitText("nihao")',
  );
});

test('cancelComposition delegates to the injected editing engine', () => {
  const deps = makeDeps();
  const handle = createTextInput(deps);
  handle.cancelComposition();
  assert.ok(
    deps.editingEngine.calls.some(([name]) => name === 'cancelComposition'),
    'fake engine must receive cancelComposition()',
  );
});

test('setSelection delegates to the injected editing engine', () => {
  const deps = makeDeps();
  const handle = createTextInput(deps);
  handle.setSelection(2, 5);
  assert.ok(
    deps.editingEngine.calls.some(([name, a, b]) => name === 'setSelection' && a === 2 && b === 5),
    'fake engine must receive setSelection(2, 5)',
  );
});

test('no edit path touches engine internals directly', () => {
  const deps = makeDeps();
  const handle = createTextInput(deps);
  handle.setComposingText('x');
  handle.commitText('x');
  handle.setSelection(0, 1);
  handle.cancelComposition();
  const leaked = Object.keys(handle).filter((k) =>
    /engine|gapBuffer|inputConnection|_buffer|_internal/i.test(k),
  );
  assert.deepEqual(leaked, [], 'handle must not expose engine internals');
  assert.ok(
    deps.editingEngine.calls.length > 0,
    'all edits must flow through the injected engine adapter',
  );
});

// ---------------------------------------------------------------------------
// Boundary / error cases: selection clamp, empty-clipboard no-op
// ---------------------------------------------------------------------------

test('out-of-range selection clamps instead of throwing', () => {
  const deps = makeDeps();
  const handle = createTextInput(deps);
  assert.doesNotThrow(() => handle.setSelection(-5, 10_000));
  const sel = handle.getSelection();
  assert.ok(sel.start >= 0 && sel.end >= 0, 'clamped selection must stay non-negative');
});

test('clipboard read with an empty clipboard is a no-op', () => {
  const deps = makeDeps({ clipboard: fakeClipboard({ text: '' }) });
  const handle = createTextInput(deps);
  const before = handle.value;
  assert.doesNotThrow(() => handle.paste());
  assert.equal(handle.value, before, 'paste with empty clipboard must not change the value');
});

// ---------------------------------------------------------------------------
// Binding contract: committed value syncs through the injected binding
// ---------------------------------------------------------------------------

test('commit writes through to the injected two-way binding', () => {
  const binding = fakeBinding('');
  const handle = createTextInput(makeDeps({ binding }));
  handle.commitText('hello');
  assert.equal(binding.value, 'hello', 'bound value must equal committed text');
  assert.equal(handle.value, 'hello', 'handle.value must equal committed text');
});

// ---------------------------------------------------------------------------
// Registry surface: manifest entries + package exports + descriptor shape
// ---------------------------------------------------------------------------

test('manifest has a tier-2 TextInput entry backed by TextFieldEditCore', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  const entry = byName.get('TextInput');
  assert.ok(entry, 'manifest must contain a TextInput entry');
  assert.equal(entry.tier, 2);
  assert.equal(entry.native, 'TextFieldEditCore');
  for (const prop of ['value', 'selection', 'composing', 'contentType', 'onSelectionChange']) {
    assert.ok(entry.props && entry.props[prop] !== undefined, `TextInput schema must declare ${prop}`);
  }
});

test('manifest keeps the tier-1 TextFieldEditCore entry (edit-core reuse)', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  const core = byName.get('TextFieldEditCore');
  assert.ok(core, 'manifest must keep the tier-1 TextFieldEditCore entry');
  assert.equal(core.tier, 1);
});

test('TextInput constructs from TSX props per its manifest schema', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  const schemaKeys = new Set(Object.keys(byName.get('TextInput').props ?? {}));
  const props = { value: 'hi', selection: { start: 0, end: 2 }, contentType: 'plain' };
  for (const key of Object.keys(props)) {
    assert.ok(schemaKeys.has(key), `TextInput.${key} is not in the manifest schema`);
  }
  assert.ok(components.TextInput, 'package entry must export TextInput');
  const descriptor = components.TextInput(props);
  assert.equal(descriptor.type, 'TextInput', 'descriptor type must equal the component name');
  assert.deepEqual(descriptor.props, props, 'descriptor must keep the { type, props } shape');
});

test('existing exports are not renamed', () => {
  assert.equal(typeof components.TextFieldEditCore, 'function', 'tier-1 edit core must stay exported');
  assert.equal(typeof components.Surface, 'function', 'existing tier-1 exports must stay');
  assert.equal(typeof components.Button, 'function', 'existing tier-2 exports must stay');
});

test('index.d.ts declares TextInput prop types from the manifest schema', () => {
  const dts = readFileSync(DTS_URL, 'utf8');
  assert.ok(dts.includes('TextInputProps'), 'index.d.ts must declare TextInputProps');
  assert.ok(dts.includes('TextInputHandle'), 'index.d.ts must declare TextInputHandle');
  assert.ok(dts.includes('createTextInput'), 'index.d.ts must declare createTextInput');
});

// ---------------------------------------------------------------------------
// T2 / AC-M3-011: IME composition preview/commit/cancel with bound-value sync
// (TC-M3-011). Extends the T1 shape tests above, which stay intact.
// Preview renders composing text without mutating the committed `value`;
// `commitText` replaces the composing range and writes through to the bound
// value synchronously; `cancelComposition` withdraws the preview leaving
// committed text untouched. `handle.value` always equals visible committed
// text after each call.
// ---------------------------------------------------------------------------

test('scripted session preview/commit/cancel yields "" -> "" -> "nihao" -> "nihao" with bound value in sync', () => {
  const binding = fakeBinding('');
  const deps = makeDeps({ binding });
  const handle = createTextInput(deps);
  const committed: string[] = [];
  const bound: string[] = [];
  const snap = () => {
    committed.push(handle.value);
    bound.push(binding.value);
  };
  handle.setComposingText('ni');
  snap();
  handle.setComposingText('nih');
  snap();
  handle.commitText('nihao');
  snap();
  handle.setComposingText('x');
  snap();
  handle.cancelComposition();
  snap();
  assert.deepEqual(
    committed,
    ['', '', 'nihao', 'nihao', 'nihao'],
    'committed sequence must be "" -> "" -> "nihao" -> "nihao"',
  );
  assert.deepEqual(bound, committed, 'bound value must equal visible committed text after every step');
});

test('preview renders composing text without mutating the committed value', () => {
  const binding = fakeBinding('');
  const deps = makeDeps({ binding });
  const handle = createTextInput(deps);
  handle.commitText('nihao');
  handle.setComposingText('nihao-x-preview');
  assert.equal(handle.value, 'nihao', 'handle.value must stay the committed text during preview');
  assert.equal(binding.value, 'nihao', 'bound value must stay the committed text during preview');
  assert.ok(
    deps.editingEngine.calls.some(
      ([name, arg]) => name === 'setComposingText' && arg === 'nihao-x-preview',
    ),
    'preview must still reach the injected editing engine',
  );
});

test('commitText replaces the composing range and writes through to the bound value synchronously', () => {
  const binding = fakeBinding('');
  const deps = makeDeps({ binding });
  const handle = createTextInput(deps);
  handle.setComposingText('nih');
  handle.commitText('nihao');
  assert.equal(handle.value, 'nihao', 'handle.value must equal the committed text');
  assert.equal(binding.value, 'nihao', 'bound value must equal the committed text synchronously');
  assert.deepEqual(binding.pushes, ['nihao'], 'exactly one synchronous write-through is expected');
  assert.ok(
    deps.editingEngine.calls.some(([name, arg]) => name === 'commitText' && arg === 'nihao'),
    'commit must reach the injected editing engine',
  );
});

test('commit writes through via the binding set() method form when present', () => {
  let current = '';
  const seen: string[] = [];
  const methodBinding = {
    get value() {
      return current;
    },
    set value(v: string) {
      current = v;
    },
    set(v: string) {
      seen.push(v);
      current = v;
    },
  };
  const handle = createTextInput(makeDeps({ binding: methodBinding }));
  handle.commitText('nihao');
  assert.equal(handle.value, 'nihao');
  assert.equal(methodBinding.value, 'nihao');
  assert.deepEqual(seen, ['nihao'], 'write-through must use the binding set() method');
});

test('cancelComposition withdraws the preview and never leaks it into the value or binding', () => {
  const binding = fakeBinding('');
  const deps = makeDeps({ binding });
  const handle = createTextInput(deps);
  handle.commitText('nihao');
  binding.pushes.length = 0;
  handle.setComposingText('x');
  handle.cancelComposition();
  assert.equal(handle.value, 'nihao', 'committed text must be untouched by cancel');
  assert.equal(binding.value, 'nihao', 'bound value must be untouched by cancel');
  assert.deepEqual(binding.pushes, [], 'cancel must not push to the binding');
  assert.ok(
    !deps.editingEngine.calls.some(([name, arg]) => name === 'commitText' && arg === 'x'),
    'preview text must never be committed to the engine on cancel',
  );
  assert.ok(
    deps.editingEngine.calls.some(([name]) => name === 'cancelComposition'),
    'cancel must reach the injected editing engine',
  );
});

test('cancelComposition with no active preview is a no-op that changes nothing', () => {
  const binding = fakeBinding('nihao');
  const deps = makeDeps({ binding });
  const handle = createTextInput(deps);
  handle.commitText('nihao');
  const callsBefore = deps.editingEngine.calls.length;
  assert.doesNotThrow(() => handle.cancelComposition());
  assert.equal(handle.value, 'nihao');
  assert.equal(binding.value, 'nihao');
  assert.equal(
    deps.editingEngine.calls.length,
    callsBefore + 1,
    'only the cancelComposition delegation is expected',
  );
});

test('committing an empty string clears the value and syncs the binding', () => {
  const binding = fakeBinding('nihao');
  const deps = makeDeps({ binding });
  const handle = createTextInput(deps);
  handle.commitText('nihao');
  handle.setComposingText('x');
  handle.commitText('');
  assert.equal(handle.value, '', 'committed empty text must clear the visible value');
  assert.equal(binding.value, '', 'bound value must clear with the committed text');
});
