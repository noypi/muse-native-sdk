'use strict';
// T11b — FR-DX-005 / AC-DX-022 / TC-DX-022 (Batch 5).
// Hermetic: node:test + node:assert/strict only. Registries are plain
// objects built inline; no editor, no filesystem, no network.
// Interface under test (plan verbatim):
//   complete(prefix, registries) => Array<{ label, kind, doc }>
// Registry shape under test (ASSUMPTION, see contract T11.md):
//   { components: [{ name, props?: {...}, events?: [{ name }],
//     enums?: [{ name, values: [] }] }], tokens?: [string] }
// Kind vocabulary (ASSUMPTION): 'prop' | 'event' | 'enum' | 'token'.
// Matching is case-insensitive substring on the label.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'editor-assist.js');

function load() {
  return require(LIB);
}

function registries() {
  return {
    components: [
      {
        name: 'Button',
        props: { color: { type: 'token' }, enabled: { type: 'boolean' }, label: { type: 'string', required: true } },
        events: [{ name: 'onPress' }, { name: 'onLongPress' }],
        enums: [{ name: 'Variant', values: ['filled', 'outlined', 'text'] }],
      },
      {
        name: 'Text',
        props: { variant: { type: 'string' }, color: { type: 'token' } },
        events: [],
        enums: [],
      },
    ],
    tokens: ['color.primary', 'color.onSurface', 'spacing.md'],
  };
}

function assertItem(item) {
  assert.ok(typeof item.label === 'string' && item.label.length > 0, 'label must be a non-empty string');
  assert.ok(['prop', 'event', 'enum', 'token'].includes(item.kind), `unexpected kind ${item.kind}`);
  assert.ok(typeof item.doc === 'string' && item.doc.length > 0, 'doc must be a non-empty string');
}

describe('complete props', () => {
  it('offers prop completions for a prop prefix', () => {
    const { complete } = load();
    const out = complete('col', registries());
    assert.ok(Array.isArray(out), 'must return an array');
    assert.ok(out.some((c) => c.label === 'color' && c.kind === 'prop'), 'expected a color prop completion');
    for (const c of out) assertItem(c);
  });

  it('returns an empty array for a prefix with no match', () => {
    const { complete } = load();
    assert.deepEqual(complete('zzz-no-such-thing', registries()), []);
  });
});

describe('complete events and enums', () => {
  it('offers event completions for an event prefix', () => {
    const { complete } = load();
    const out = complete('onP', registries());
    assert.ok(out.some((c) => c.label === 'onPress' && c.kind === 'event'), 'expected an onPress event completion');
    for (const c of out) assertItem(c);
  });

  it('offers enum value completions for an enum prefix', () => {
    const { complete } = load();
    const out = complete('fil', registries());
    assert.ok(out.some((c) => c.label === 'filled' && c.kind === 'enum'), 'expected a filled enum completion');
    for (const c of out) assertItem(c);
  });

  it('offers token completions from the design-token registry', () => {
    const { complete } = load();
    const out = complete('color.', registries());
    assert.ok(out.some((c) => c.label === 'color.primary' && c.kind === 'token'), 'expected a token completion');
    for (const c of out) assertItem(c);
  });
});

describe('complete errors', () => {
  it('rejects a null registries map with a structured error', () => {
    const { complete } = load();
    assert.throws(() => complete('col', null), (err) => {
      assert.ok(err instanceof Error, 'must throw an Error');
      assert.ok(/^E_CODEGEN_/.test(err.code), `unexpected code ${err.code}`);
      assert.match(String(err.cause), /DX-005/);
      return true;
    });
  });

  it('rejects a non-string prefix with a structured error', () => {
    const { complete } = load();
    assert.throws(() => complete(42, registries()), (err) => {
      assert.ok(err instanceof Error, 'must throw an Error');
      assert.ok(/^E_CODEGEN_/.test(err.code), `unexpected code ${err.code}`);
      return true;
    });
  });
});
