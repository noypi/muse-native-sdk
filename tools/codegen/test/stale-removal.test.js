'use strict';
// T12 — FR-DX-005 / AC-DX-023 / TC-DX-023 (Batch 5).
// Hermetic: node:test + node:assert/strict only. Fixtures are plain objects
// built inline; generation and sync-check are reused from T10/T11 libs via
// require (no filesystem, no TS compiler, no CI, no network).
// Interface under test (plan verbatim):
//   none — asserts the T10/T11 contract end-to-end (no new callable surface)
// Scenario: remove one native method from the fixture manifest, regenerate,
// and assert (a) the old call-site string no longer type-resolves against
// the new declarations (simulated: the method name is absent from the
// regenerated file while the call-site fixture still references it) and
// (b) checkInSync against the pre-removal committed files fails naming the
// stale module.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const GEN = path.resolve(__dirname, '..', 'lib', 'generate.js');
const SYNC = path.resolve(__dirname, '..', 'lib', 'sync-check.js');

function loadGen() {
  return require(GEN);
}

function loadSync() {
  return require(SYNC);
}

function fullManifest() {
  return {
    components: [
      {
        name: 'Button',
        props: { label: { type: 'string', required: true } },
        methods: [
          { name: 'measure', params: ['handle'], returns: 'object' },
          { name: 'focus', params: [], returns: 'void' },
        ],
        events: [{ name: 'onPress', payload: 'object' }],
        enums: [{ name: 'Variant', values: ['filled', 'outlined'] }],
      },
    ],
  };
}

// Call-site fixture: handwritten JS that calls the method under test.
// After removal + regeneration this reference must dangle.
const CALL_SITE = 'Button.focus(); // handwritten call site';

describe('stale-declaration failure proof', () => {
  it('drops the removed method from the regenerated declaration', () => {
    const { generateDeclarations } = loadGen();
    const before = generateDeclarations(fullManifest()).files['Button'];
    assert.ok(before.includes('focus'), 'pre-removal declaration must contain focus');

    const trimmed = fullManifest();
    trimmed.components[0].methods = trimmed.components[0].methods.filter((m) => m.name !== 'focus');
    const after = generateDeclarations(trimmed).files['Button'];
    assert.ok(!after.includes('focus'), 'post-removal declaration must not contain focus');
  });

  it('leaves the old call site dangling (simulated strict-mode type error)', () => {
    const { generateDeclarations } = loadGen();
    const trimmed = fullManifest();
    trimmed.components[0].methods = trimmed.components[0].methods.filter((m) => m.name !== 'focus');
    const after = generateDeclarations(trimmed).files['Button'];
    assert.ok(CALL_SITE.includes('focus'), 'call-site fixture must still reference the removed method');
    assert.ok(!after.includes('focus'), 'regenerated declaration must no longer resolve focus');
  });

  it('fails the sync gate naming the stale module', () => {
    const { generateDeclarations } = loadGen();
    const { checkInSync } = loadSync();
    const committed = generateDeclarations(fullManifest()).files;
    const trimmed = fullManifest();
    trimmed.components[0].methods = trimmed.components[0].methods.filter((m) => m.name !== 'focus');
    const out = checkInSync(trimmed, committed);
    assert.equal(out.inSync, false);
    assert.ok(out.stale.includes('Button'), 'sync check must name Button stale');
  });
});
