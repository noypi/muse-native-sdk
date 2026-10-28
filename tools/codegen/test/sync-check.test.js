'use strict';
// T11a — FR-DX-005 / AC-DX-021 / TC-DX-021 (Batch 5).
// Hermetic: node:test + node:assert/strict only. Manifests and committed
// files are plain objects built inline; generation is reused from T10's
// lib via require (no filesystem, no TS compiler, no CI, no network).
// Interface under test (plan verbatim):
//   checkInSync(manifest, committedFiles) => { inSync: boolean, stale: string[] }
// Rule (per plan Detail): committedFiles are string-compared against a fresh
// T10 regeneration; the stale list is the mismatch set (missing module,
// drifted content, or extra committed module all count as stale).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'sync-check.js');
const GEN = path.resolve(__dirname, '..', 'lib', 'generate.js');

function load() {
  return require(LIB);
}

function fixture() {
  return {
    components: [
      {
        name: 'Button',
        props: { label: { type: 'string', required: true }, enabled: { type: 'boolean' } },
        methods: [{ name: 'measure', params: ['handle'], returns: 'object' }],
        events: [{ name: 'onPress', payload: 'object' }],
        enums: [{ name: 'Variant', values: ['filled', 'outlined'] }],
      },
      { name: 'Text', props: { variant: { type: 'string' } }, methods: [], events: [], enums: [] },
    ],
  };
}

function committedFor(manifest) {
  return require(GEN).generateDeclarations(manifest).files;
}

describe('checkInSync', () => {
  it('reports in sync when committed files match a fresh regeneration', () => {
    const { checkInSync } = load();
    const manifest = fixture();
    const out = checkInSync(manifest, committedFor(manifest));
    assert.equal(out.inSync, true);
    assert.deepEqual(out.stale, []);
  });

  it('reports a drifted committed file as stale', () => {
    const { checkInSync } = load();
    const manifest = fixture();
    const committed = committedFor(manifest);
    committed['Button'] = '// hand-edited\n' + committed['Button'];
    const out = checkInSync(manifest, committed);
    assert.equal(out.inSync, false);
    assert.ok(out.stale.includes('Button'), 'drifted module must be listed stale');
  });

  it('reports a missing committed module as stale', () => {
    const { checkInSync } = load();
    const manifest = fixture();
    const committed = committedFor(manifest);
    delete committed['Text'];
    const out = checkInSync(manifest, committed);
    assert.equal(out.inSync, false);
    assert.ok(out.stale.includes('Text'), 'missing module must be listed stale');
  });

  it('reports an extra committed module as stale', () => {
    const { checkInSync } = load();
    const manifest = fixture();
    const committed = committedFor(manifest);
    committed['Ghost'] = '/* AUTO-GENERATED — do not hand-edit (DX-005) */\nexport type GhostProps = {};';
    const out = checkInSync(manifest, committed);
    assert.equal(out.inSync, false);
    assert.ok(out.stale.includes('Ghost'), 'extra module must be listed stale');
  });

  it('treats an empty manifest with empty committed files as in sync', () => {
    const { checkInSync } = load();
    const out = checkInSync({ components: [] }, {});
    assert.equal(out.inSync, true);
    assert.deepEqual(out.stale, []);
  });

  it('rejects a null manifest with a structured error', () => {
    const { checkInSync } = load();
    assert.throws(() => checkInSync(null, {}), (err) => {
      assert.ok(err instanceof Error, 'must throw an Error');
      assert.ok(/^E_CODEGEN_/.test(err.code), `unexpected code ${err.code}`);
      assert.match(String(err.cause), /DX-005/);
      return true;
    });
  });

  it('rejects a null committedFiles map with a structured error', () => {
    const { checkInSync } = load();
    assert.throws(() => checkInSync(fixture(), null), (err) => {
      assert.ok(err instanceof Error, 'must throw an Error');
      assert.ok(/^E_CODEGEN_/.test(err.code), `unexpected code ${err.code}`);
      return true;
    });
  });
});
