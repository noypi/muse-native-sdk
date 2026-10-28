'use strict';
// TC-PKG-006 (AC-PKG-006) — Batch 2 (T4 contract).
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

// ASSUMPTION: manifest `exports` entries carry
// `{ name: string, arity: number, argKinds: string[], returnKind: string }`.
// The plan pins the symbol-record shape but not the manifest entry fields;
// name/arity/kinds are the minimum the acceptance criteria imply.
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
      { name: 'step', arity: 2, argKinds: ['handle', 'number'], returnKind: 'void' },
    ],
    capabilities: [],
    ...overrides,
  };
}

function matchingSymbols() {
  return {
    createWorld: { arity: 0, argKinds: [], returnKind: 'handle', fn: () => 1 },
    step: { arity: 2, argKinds: ['handle', 'number'], returnKind: 'void', fn: () => {} },
  };
}

function assertPkgError(fn, expectedCode, expectedPathPart) {
  let err;
  try {
    fn();
  } catch (e) {
    err = e;
  }
  assert.ok(err, 'must throw a PkgError');
  assert.equal(err.code, expectedCode, `code must be ${expectedCode}, got: ${err && err.code}`);
  assert.equal(typeof err.path, 'string', 'PkgError must carry a string path');
  assert.equal(typeof err.expected, 'string', 'PkgError must carry a string expected');
  if (expectedPathPart !== undefined) {
    assert.ok(
      String(err.path).includes(expectedPathPart),
      `error path must name "${expectedPathPart}", got: ${err.path}`,
    );
  }
  return err;
}

// ---------------------------------------------------------------------------
// bindExports — T4 contract:
// `bindExports(manifest, symbols): Record<string, Function>`
// throws `PkgError{ code: "E_EXPORT_BINDING", path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-006 — bindExports (AC-PKG-006)', () => {
  it('binds every manifest-listed symbol to a callable function', () => {
    const bound = bindExports(validManifest(), matchingSymbols());
    assert.equal(typeof bound.createWorld, 'function');
    assert.equal(typeof bound.step, 'function');
    assert.equal(bound.createWorld(), 1);
  });

  it('refuses a manifest entry with no matching symbol naming the symbol (E_EXPORT_BINDING)', () => {
    const symbols = matchingSymbols();
    delete symbols.step;
    assertPkgError(() => bindExports(validManifest(), symbols), 'E_EXPORT_BINDING', 'step');
  });

  it('refuses when the symbol table is empty but exports are declared', () => {
    assertPkgError(() => bindExports(validManifest(), {}), 'E_EXPORT_BINDING', 'createWorld');
  });

  it('refuses an arity mismatch naming the symbol (ASSUMPTION)', () => {
    const symbols = matchingSymbols();
    symbols.step = { ...symbols.step, arity: 1 };
    assertPkgError(() => bindExports(validManifest(), symbols), 'E_EXPORT_BINDING', 'step');
  });

  it('refuses an arg-kind mismatch naming the symbol (ASSUMPTION)', () => {
    const symbols = matchingSymbols();
    symbols.step = { ...symbols.step, argKinds: ['handle', 'string'] };
    assertPkgError(() => bindExports(validManifest(), symbols), 'E_EXPORT_BINDING', 'step');
  });

  it('ignores unlisted library symbols in the bound surface', () => {
    const bound = bindExports(validManifest(), { ...matchingSymbols(), debugBackdoor: { arity: 0, argKinds: [], returnKind: 'void', fn: () => {} } });
    assert.equal(bound.debugBackdoor, undefined);
  });

  it('binds an empty export list to an empty record', () => {
    assert.deepEqual(bindExports(validManifest({ exports: [] }), matchingSymbols()), {});
  });

  it('rejects a symbol entry whose fn is not callable (security: malformed table)', () => {
    const symbols = matchingSymbols();
    symbols.step = { ...symbols.step, fn: 'not-a-function' };
    assertPkgError(() => bindExports(validManifest(), symbols), 'E_EXPORT_BINDING', 'step');
  });
});
