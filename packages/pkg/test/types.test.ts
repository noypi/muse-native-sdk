'use strict';
// TC-PKG-007 (AC-PKG-007) — Batch 2 (T4 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/d.ts strings;
// no network, no secrets, no live services, no fs fixtures, no tsc subprocess
// (full `tsc --noEmit` verification is tester-layer work per plan Risk).
// Binds ONLY to the pinned T4 `Interface:` line, never to an implementation.
// Resolution: `../lib/types.js` once it lands, else the `../index.js` shim,
// so every behavior case below fails until T4 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let typesMod = {};
try {
  typesMod = require('../lib/types.js');
} catch {
  try {
    typesMod = require('../index.js');
  } catch {
    typesMod = {};
  }
}
const { checkEntryTypes } = typesMod;

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

const MATCHING_DTS = `export declare function createWorld(): number;
export declare function step(world: number, dt: number): void;
`;

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
// checkEntryTypes — T4 contract:
// `checkEntryTypes(manifest, dts): void`
// throws `PkgError{ code: "E_TYPES", path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-007 — checkEntryTypes (AC-PKG-007)', () => {
  it('accepts a d.ts declaring every export with matching arity', () => {
    assert.doesNotThrow(() => checkEntryTypes(validManifest(), MATCHING_DTS));
  });

  it('refuses a d.ts missing an export declaration naming the symbol (E_TYPES)', () => {
    const dts = `export declare function createWorld(): number;\n`;
    assertPkgError(() => checkEntryTypes(validManifest(), dts), 'E_TYPES', 'step');
  });

  it('refuses a wrong-arity declaration naming the symbol', () => {
    const dts = `export declare function createWorld(): number;
export declare function step(world: number): void;
`;
    assertPkgError(() => checkEntryTypes(validManifest(), dts), 'E_TYPES', 'step');
  });

  it('refuses a wrong-arg-kind declaration naming the symbol (ASSUMPTION: kinds map to TS types)', () => {
    const dts = `export declare function createWorld(): number;
export declare function step(world: number, dt: string): void;
`;
    assertPkgError(() => checkEntryTypes(validManifest(), dts), 'E_TYPES', 'step');
  });

  it('refuses an empty d.ts when exports are declared', () => {
    assertPkgError(() => checkEntryTypes(validManifest(), ''), 'E_TYPES', 'createWorld');
  });

  it('accepts any d.ts when no exports are declared (ASSUMPTION)', () => {
    assert.doesNotThrow(() => checkEntryTypes(validManifest({ exports: [] }), ''));
  });

  it('ignores unlisted declarations in the d.ts', () => {
    const dts = `${MATCHING_DTS}export declare function debugBackdoor(): void;\n`;
    assert.doesNotThrow(() => checkEntryTypes(validManifest(), dts));
  });

  it('rejects a d.ts smuggling an executable script tag (security: injection)', () => {
    const dts = `${MATCHING_DTS}<script>evil()</script>\n`;
    assertPkgError(() => checkEntryTypes(validManifest(), dts), 'E_TYPES', 'src/index.d.ts');
  });
});
