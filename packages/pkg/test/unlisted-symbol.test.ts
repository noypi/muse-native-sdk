'use strict';
// TC-PKG-008 (AC-PKG-008) — Batch 2 (T4 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/symbol fakes;
// no network, no secrets, no live services, no fs fixtures.
// Binds ONLY to the pinned T4 `Interface:` line, never to an implementation.
// Resolution: `../lib/exports.js` once it lands, else the `../index.js` shim,
// so every behavior case below fails until T4 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let exportsMod = {};
try {
  exportsMod = require('../lib/exports.js');
} catch {
  try {
    exportsMod = require('../index.js');
  } catch {
    exportsMod = {};
  }
}
const { bindExports } = exportsMod;

function validManifest(overrides = {}) {
  return {
    name: 'acme-engine',
    version: '1.2.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: ['arm64-v8a'],
    engineAbi: 'deadbeef',
    exports: [
      { name: 'createWorld', arity: 0, argKinds: [], returnKind: 'handle' },
    ],
    capabilities: [],
    ...overrides,
  };
}

function symbolsWithExtras() {
  return {
    createWorld: { arity: 0, argKinds: [], returnKind: 'handle', fn: () => 1 },
    nativeBackdoor: { arity: 0, argKinds: [], returnKind: 'void', fn: () => 'pwned' },
    __internal_reset: { arity: 0, argKinds: [], returnKind: 'void', fn: () => {} },
  };
}

// ---------------------------------------------------------------------------
// Unlisted symbols must be unreachable from JS (AC-PKG-008) via:
// `bindExports(manifest, symbols): Record<string, Function>`
// ---------------------------------------------------------------------------
describe('TC-PKG-008 — unlisted symbols unreachable (AC-PKG-008)', () => {
  it('exposes exactly the manifest-listed keys and nothing more', () => {
    const bound = bindExports(validManifest(), symbolsWithExtras());
    assert.deepEqual(Object.keys(bound).sort(), ['createWorld']);
  });

  it('leaves an unlisted symbol undefined on the bound surface', () => {
    const bound = bindExports(validManifest(), symbolsWithExtras());
    assert.equal(bound.nativeBackdoor, undefined);
  });

  it('leaves an underscore-prefixed internal symbol unreachable', () => {
    const bound = bindExports(validManifest(), symbolsWithExtras());
    assert.equal(bound.__internal_reset, undefined);
  });

  it('does not let an unlisted __proto__ entry pollute the bound object (security)', () => {
    const symbols = { createWorld: { arity: 0, argKinds: [], returnKind: 'handle', fn: () => 1 } };
    Object.defineProperty(symbols, '__proto__', {
      value: { arity: 0, argKinds: [], returnKind: 'void', fn: () => 'pwned' },
      enumerable: true,
    });
    const bound = bindExports(validManifest(), symbols);
    assert.equal(Object.getPrototypeOf(bound), Object.prototype);
    assert.equal(bound.polluted, undefined);
  });

  it('bound functions cannot reach the raw symbol table (security: no escape)', () => {
    const bound = bindExports(validManifest(), symbolsWithExtras());
    assert.equal(typeof bound.createWorld, 'function');
    assert.equal(bound.createWorld.nativeBackdoor, undefined);
    assert.equal(bound.symbols, undefined);
  });
});
