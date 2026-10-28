'use strict';
// TC-PKG-009 gallery half (AC-PKG-009) — Batch 3 (T5 contract).
// Hermetic: node:test + node:assert/strict only. Inline fake package exports
// and an in-memory engine-primitive recorder; no network, no secrets, no live
// services, no fs fixtures, no real renderer.
// Binds ONLY to the T5 Detail line ("gallery test renders a JS-only package's
// components") via the assumed gallery helper below — never to an implementation.
// Resolution: `../../src/js-package.js` once it lands, else `{}`,
// so every behavior case below fails until the gallery helper lands.
//
// ASSUMPTION: the gallery exposes `renderJsPackage(pkg, prim)` where `pkg` is
// the exports object resolved through `importPackage` (shape
// `{ components: Array<{ name: string, geometry: object }> }`) and `prim` is the
// engine-primitive factory. The coder reconciles this signature; behavior below
// (engine-primitives-only rendering) is what must hold.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let galleryMod = {};
try {
  galleryMod = require('../src/js-package.js');
} catch {
  galleryMod = {};
}
const { renderJsPackage } = galleryMod;

function fakePackage(overrides = {}) {
  return {
    components: [
      { name: 'Button', geometry: { w: 120, h: 48 } },
      { name: 'Chip', geometry: { w: 64, h: 32 } },
    ],
    ...overrides,
  };
}

// In-memory engine-primitive recorder: the only drawing surface the test sees.
function recorder() {
  const calls = [];
  const prim = {
    box: (data) => calls.push({ op: 'box', data }),
    text: (data) => calls.push({ op: 'text', data }),
  };
  return { prim, calls };
}

describe('TC-PKG-009 — gallery renders a JS-only package (AC-PKG-009)', () => {
  it('renders each exported component through engine primitives', () => {
    const { prim, calls } = recorder();
    const out = renderJsPackage(fakePackage(), prim);
    assert.equal(calls.filter((c) => c.op === 'box').length, 2);
    assert.ok(calls.some((c) => c.data.name === 'Button'));
    assert.ok(calls.some((c) => c.data.name === 'Chip'));
    assert.ok(out !== undefined);
  });

  it('maps plain component data onto primitives with zero drawing calls into the package', () => {
    let packageDrawn = false;
    const pkg = fakePackage();
    pkg.draw = () => {
      packageDrawn = true;
    };
    const { prim, calls } = recorder();
    renderJsPackage(pkg, prim);
    assert.equal(packageDrawn, false, 'must never call drawing code inside the package');
    assert.ok(calls.every((c) => c.op === 'box' || c.op === 'text'));
    assert.ok(calls.every((c) => typeof c.data === 'object'));
  });

  it('accepts the exports object resolved through the same importPackage form', () => {
    // The object handed to the gallery is exactly what `importPackage`
    // resolves to for a JS-only package: plain data, no loader handles.
    const resolved = { components: [{ name: 'Badge', geometry: { w: 24, h: 24 } }] };
    const { prim, calls } = recorder();
    renderJsPackage(resolved, prim);
    assert.equal(calls.length, 1);
    assert.equal(calls[0].data.name, 'Badge');
  });

  it('renders nothing and throws nothing for empty exports', () => {
    const { prim, calls } = recorder();
    const out = renderJsPackage({ components: [] }, prim);
    assert.equal(calls.length, 0);
    assert.ok(out !== undefined);
  });

  it('never renders unlisted symbols from the package object', () => {
    const pkg = { ...fakePackage(), secretNative: { name: 'Secret', geometry: { w: 1, h: 1 } } };
    const { prim, calls } = recorder();
    renderJsPackage(pkg, prim);
    assert.ok(calls.every((c) => c.data.name !== 'Secret'));
  });

  it('fails closed with a named error on a malformed component', () => {
    const pkg = { components: [{ name: 'Broken' }] };
    const { prim } = recorder();
    assert.throws(() => renderJsPackage(pkg, prim), (err) => {
      assert.equal(typeof err.code, 'string');
      assert.ok(String(err.path).includes('Broken'));
      return true;
    });
  });

  it('rejects an oversized component payload instead of rendering it (boundary)', () => {
    const big = new Array(100000).fill(0);
    const pkg = { components: [{ name: 'Huge', geometry: { points: big } }] };
    const { prim } = recorder();
    assert.throws(() => renderJsPackage(pkg, prim), (err) => {
      assert.equal(typeof err.code, 'string');
      return true;
    });
  });
});
