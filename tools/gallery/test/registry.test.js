'use strict';
// T12 — FR-DX-007 (AC-DX-030, AC-DX-032-part): gallery generated purely from
// the registry input — adding a new entry produces a new page with zero
// gallery edits, and no hardcoded component list anywhere in gallery code.
// This test imports the T11 renderer only to pin delegation equivalence; the
// renderer itself is NEVER inlined here (RED with MODULE_NOT_FOUND before the
// T11 coder runs is the expected red state — do not inline it).
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no
// secrets, no application config, no live services, no filesystem, no clock.
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T12.md):
// - A-T12a (plan A4): entries are `{ name, props, tokens, a11y }` stubs,
//   same shape contract as T11.
// - A-T12b: generation is pure — same registry in, same pages out; input
//   array is not mutated.
// - A-T12c: empty registry yields `[]`; non-array input throws TypeError.
// - A-T12d: "no hardcoded component list" is pinned behaviorally: two
//   registries with distinct synthetic names each produce exactly their own
//   pages, so a hardcoded list cannot pass.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { galleryFromRegistry } = require('../lib/from-registry.js');
const { renderGallery } = require('../lib/gallery.js');

function entry(name) {
  return {
    name,
    props: { [`${name}Prop`]: 'string' },
    tokens: { [`${name}Token`]: '#123456' },
    a11y: { role: 'generic', label: name },
  };
}

describe('galleryFromRegistry — pure generation over registry input', () => {
  it('produces one page per registry entry', () => {
    const pages = galleryFromRegistry([entry('Alpha'), entry('Beta')]);
    assert.equal(pages.length, 2);
    assert.deepEqual(
      pages.map((p) => p.name).sort(),
      ['Alpha', 'Beta'],
    );
  });

  it('grows by exactly one page when a new entry is added (zero gallery edits)', () => {
    const before = galleryFromRegistry([entry('Alpha'), entry('Beta')]);
    const after = galleryFromRegistry([entry('Alpha'), entry('Beta'), entry('ZetaWidgetXyz')]);
    assert.equal(after.length, before.length + 1);
    assert.ok(after.some((p) => p.name === 'ZetaWidgetXyz'));
  });

  it('follows the input, not a hardcoded list (distinct synthetic registries)', () => {
    const first = galleryFromRegistry([entry('QuuxOne'), entry('QuuxTwo')]);
    const second = galleryFromRegistry([entry('WobbleA'), entry('WobbleB'), entry('WobbleC')]);
    assert.deepEqual(
      first.map((p) => p.name).sort(),
      ['QuuxOne', 'QuuxTwo'],
    );
    assert.deepEqual(
      second.map((p) => p.name).sort(),
      ['WobbleA', 'WobbleB', 'WobbleC'],
    );
  });

  it('matches the T11 renderer output for the same input (delegation)', () => {
    const registry = [entry('Alpha'), entry('Beta')];
    const viaRegistry = galleryFromRegistry(registry);
    const viaRenderer = renderGallery(registry, { theme: 'light', dir: 'ltr' });
    assert.equal(viaRegistry.length, viaRenderer.length);
    assert.deepEqual(
      viaRegistry.map((p) => p.name).sort(),
      viaRenderer.map((p) => p.name).sort(),
    );
  });

  it('is pure — repeated calls agree and the input array is not mutated', () => {
    const registry = [entry('Alpha'), entry('Beta')];
    const snapshot = JSON.parse(JSON.stringify(registry));
    const first = galleryFromRegistry(registry);
    const second = galleryFromRegistry(registry);
    assert.deepEqual(first, second);
    assert.deepEqual(registry, snapshot);
  });

  it('returns [] for an empty registry and throws TypeError for non-array input', () => {
    assert.deepEqual(galleryFromRegistry([]), []);
    assert.throws(() => galleryFromRegistry(null), TypeError);
    assert.throws(() => galleryFromRegistry({ name: 'Alpha' }), TypeError);
  });
});
