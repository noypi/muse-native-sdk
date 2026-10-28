'use strict';
// T10 — FR-DX-005 / AC-DX-020 / TC-DX-020 (Batch 5).
// Hermetic: node:test + node:assert/strict only. The manifest is a plain
// object built inline (manifest-shaped: { components: [{ name, props,
// methods, events, enums }] }); no filesystem, no TS compiler, no network,
// no clock, no config.
// Interface under test (plan verbatim):
//   generateDeclarations(manifest) => { files: { [moduleName: string]: string }, diagnostics: Array<{ code, cause, action }> }
// Manifest shape under test (ASSUMPTION, see contract T10.md):
//   { components: [{ name, props?: { [prop]: { type, required?, values? } },
//     methods?: [{ name, params?, returns? }], events?: [{ name, payload? }],
//     enums?: [{ name, values: [] }] }] }
// Every emitted file carries the header:
//   /* AUTO-GENERATED — do not hand-edit (DX-005) */

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'generate.js');
const HEADER = '/* AUTO-GENERATED — do not hand-edit (DX-005) */';

function load() {
  return require(LIB);
}

function manifest9() {
  // 9-module fixture mirroring packages/native/muse.manifest.json shape.
  const names = ['View', 'Text', 'Image', 'ScrollView', 'TextInput', 'Button', 'FlatList', 'TouchableOpacity', 'Modal'];
  return {
    components: names.map((name) => ({
      name,
      props: { enabled: { type: 'boolean' }, label: { type: 'string', required: true } },
      methods: [{ name: 'measure', params: ['handle'], returns: 'object' }],
      events: [{ name: 'onPress', payload: 'object' }],
      enums: [{ name: 'Variant', values: ['filled', 'outlined'] }],
    })),
  };
}

function assertNoLeak(err) {
  assert.ok(err instanceof Error, 'must throw an Error');
  assert.doesNotMatch(String(err.stack ?? '').split('\n').slice(1).join('\n'), /node_modules/);
  return true;
}

describe('generateDeclarations files', () => {
  it('emits one declaration file per manifest component', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations(manifest9());
    assert.equal(Object.keys(out.files).length, 9);
    for (const name of ['View', 'Text', 'Image', 'ScrollView', 'TextInput', 'Button', 'FlatList', 'TouchableOpacity', 'Modal']) {
      assert.ok(typeof out.files[name] === 'string', `missing file for ${name}`);
    }
  });

  it('heads every emitted file with the DX-005 AUTO-GENERATED header', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations(manifest9());
    for (const [name, text] of Object.entries(out.files)) {
      assert.ok(text.includes(HEADER), `${name} missing AUTO-GENERATED header`);
    }
  });

  it('gives every method, prop and event a JSDoc line', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations(manifest9());
    const text = out.files['Button'];
    assert.ok(text.includes('/**'), 'expected a JSDoc comment block');
    for (const member of ['measure', 'enabled', 'label', 'onPress']) {
      const idx = text.indexOf(member);
      assert.ok(idx > 0, `member ${member} missing from declaration`);
      assert.ok(text.slice(0, idx).includes('/**'), `member ${member} has no preceding JSDoc`);
    }
  });

  it('emits a typed descriptor for an empty-props stub', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations({ components: [{ name: 'Stub', props: {} }] });
    assert.ok(typeof out.files['Stub'] === 'string', 'empty-props stub must still emit a file');
    assert.ok(out.files['Stub'].includes('Stub'), 'stub file must name its module');
    assert.ok(out.files['Stub'].includes(HEADER), 'stub file must carry the header');
  });

  it('emits enum values into the declaration', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations(manifest9());
    assert.ok(out.files['Button'].includes('filled'), 'expected enum value filled in declaration');
    assert.ok(out.files['Button'].includes('outlined'), 'expected enum value outlined in declaration');
  });

  it('marks required props non-optional and optional props with ?', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations(manifest9());
    const text = out.files['Button'];
    assert.ok(text.includes('label:'), 'required prop label must be non-optional');
    assert.ok(text.includes('enabled?'), 'optional prop enabled must use ?');
  });

  it('returns a diagnostics array (empty on success)', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations(manifest9());
    assert.ok(Array.isArray(out.diagnostics), 'diagnostics must be an array');
    for (const d of out.diagnostics) {
      assert.ok(typeof d.code === 'string' && typeof d.cause === 'string' && typeof d.action === 'string');
    }
  });
  it('maps an unknown prop type to unknown and records an E_CODEGEN_UNKNOWN_TYPE diagnostic', () => {
    const { generateDeclarations } = load();
    const out = generateDeclarations({
      components: [{ name: 'Weird', props: { mystery: { type: 'frobnicator' } } }],
    });
    assert.ok(typeof out.files['Weird'] === 'string', 'unknown-type component must still emit a file');
    assert.ok(out.files['Weird'].includes('unknown'), 'unknown type must be emitted as unknown');
    const diag = out.diagnostics.find((d) => d.code === 'E_CODEGEN_UNKNOWN_TYPE');
    assert.ok(diag, 'expected an E_CODEGEN_UNKNOWN_TYPE diagnostic');
    assert.ok(typeof diag.cause === 'string' && typeof diag.action === 'string');
  });
});

describe('generateDeclarations errors', () => {
  it('rejects a null manifest with a DX-005 structured error', () => {
    const { generateDeclarations } = load();
    assert.throws(() => generateDeclarations(null), (err) => {
      assert.equal(err.code, 'E_CODEGEN_BAD_MANIFEST');
      assert.match(String(err.cause), /DX-005/);
      assert.ok(typeof err.action === 'string' && err.action.length > 0);
      return true;
    });
  });

  it('rejects a manifest with no components array', () => {
    const { generateDeclarations } = load();
    assert.throws(() => generateDeclarations({}), (err) => {
      assert.ok(/^E_CODEGEN_/.test(err.code), `unexpected code ${err.code}`);
      assert.match(String(err.cause), /DX-005/);
      return true;
    });
  });

  it('rejects a hostile component name without leaking or polluting', () => {
    const { generateDeclarations } = load();
    assert.throws(() => generateDeclarations({ components: [{ name: '__proto__', props: {} }] }), (err) => {
      assertNoLeak(err);
      assert.ok(/^E_CODEGEN_/.test(err.code), `unexpected code ${err.code}`);
      return true;
    });
    assert.ok(!({}).polluted, 'prototype must not be polluted');
  });
});
