'use strict';
// T2 — demo stub-bundling contract pin (demo-fallback-boot Batch 1, GR-08 T1.1).
//
// The demo packaging recipe must include a resolvable stub route: at
// assemble, hash(dist/tree.mui.bin) == hash(assets/tree.mui.bin), and the
// `segment` stub route the sources require must resolve to a file that is
// also bundled into the APK (host-only ../../../proxy/lib/stub.js does not
// ride the glass path). Failure shape: `STUB_ROUTE_UNRESOLVABLE`.
//
// Binds ONLY to the plan Interface
// `assertStubRouteResolvable(apkTree): void` (recipe-text form) and the
// committed Makefile/sources — no device, no downloads, no secrets, no
// config, no network.
//
// Spec refs: FR-SAMPLE-010, AC-SAMPLE-026, AC-SAMPLE-033.
//
// Expected RED (pre-T5): the recipe plain-`cp`s the tree asset with no hash
// gate, and the stub `require` escapes the package root with no bundled
// equivalent staged into the APK — the gate cases below FAIL until the
// coder bundles the stub fetch path per the recipe.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { createHash } = require('node:crypto');
const { existsSync, readFileSync } = require('node:fs');
const { join, resolve } = require('node:path');

const DEMO_ROOT = join(__dirname, '..');
const MAKEFILE = join(DEMO_ROOT, 'Makefile');
const SRC = join(DEMO_ROOT, 'src');

function readRecipe() {
  return readFileSync(MAKEFILE, 'utf8');
}

function sha256(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

// Interface under test (recipe-text form of
// `assertStubRouteResolvable(apkTree): void`). Throws
// `STUB_ROUTE_UNRESOLVABLE(<reason>)` when the packaged APK cannot serve the
// stub route: no assemble-time hash gate, or no bundled stub equivalent.
function assertStubRouteResolvable({ recipe, stubAbs, staged }) {
  const gatesTreeHash =
    /dist\/tree\.mui\.bin/.test(recipe) &&
    /assets\/tree\.mui\.bin/.test(recipe) &&
    (/cmp\s+-s/.test(recipe) || /sha256sum/.test(recipe));
  if (!gatesTreeHash) {
    throw new Error(
      'STUB_ROUTE_UNRESOLVABLE(tree-asset): recipe packs dist/tree.mui.bin ' +
        'with no assemble-time hash gate against assets/tree.mui.bin',
    );
  }
  if (!stubAbs) {
    throw new Error(
      'STUB_ROUTE_UNRESOLVABLE(segment): stub route has no resolvable file',
    );
  }
  if (!staged) {
    throw new Error(
      `STUB_ROUTE_UNRESOLVABLE(segment): ${stubAbs} is host-only (escapes ` +
        'the package root) with no bundled equivalent staged into the APK',
    );
  }
}

// The `segment` stub the sources require. Resolves host-side today
// (proxy/lib/stub.js, outside the demo package); the bundled-equivalent
// leg asserts the recipe stages it into the APK staging tree.
function resolveStubAbs() {
  const candidates = [
    join(DEMO_ROOT, 'src', 'stub.js'),
    join(DEMO_ROOT, 'assets', 'stub.js'),
    resolve(SRC, '../../../proxy/lib/stub.js'),
  ];
  for (const c of candidates) {
    if (existsSync(c)) return c;
  }
  return null;
}

// True when the recipe copies a stub/segment-route file into the APK
// staging tree (STAGING_DIR assets), i.e. the glass path can serve it.
function recipeStagesStubEquivalent(recipe) {
  const lines = recipe.split('\n');
  return lines.some(
    (line) =>
      /STAGING_DIR/.test(line) &&
      /stub\.js|segment/.test(line) &&
      (/cp\b/.test(line) || /install\b/.test(line)),
  );
}

describe('T2 — recipe gates tree-asset hash at assemble', () => {
  it('hash(dist/tree.mui.bin) == hash(assets/tree.mui.bin) is enforced, not cp-blind', () => {
    const recipe = readRecipe();
    assertStubRouteResolvable({
      recipe,
      stubAbs: resolveStubAbs(),
      staged: recipeStagesStubEquivalent(recipe),
    });
  });
});

describe('T2 — stub route is resolvable and bundled', () => {
  it('sources require a segment stub that resolves to a file', () => {
    const stubAbs = resolveStubAbs();
    assert.ok(
      stubAbs,
      'STUB_ROUTE_UNRESOLVABLE(segment): no resolvable stub file for the ' +
        '`segment` route required by src/suggestions.ts + src/guessMask.ts',
    );
  });

  it('the resolved stub (or equivalent) is staged into the APK, not host-only', () => {
    const recipe = readRecipe();
    const stubAbs = resolveStubAbs();
    const underDemo =
      stubAbs !== null && stubAbs.startsWith(resolve(DEMO_ROOT) + '/');
    assert.ok(
      underDemo || recipeStagesStubEquivalent(recipe),
      `STUB_ROUTE_UNRESOLVABLE(segment): ${stubAbs} escapes the package ` +
        'root with no bundled equivalent staged (STAGING_DIR) for glass',
    );
  });
});

describe('T2 — tree asset bytes are the assembled bytes (informational)', () => {
  it('dist/tree.mui.bin exists and assets copy matches when present', () => {
    const dist = join(DEMO_ROOT, 'dist', 'tree.mui.bin');
    const staged = join(DEMO_ROOT, 'assets', 'tree.mui.bin');
    // The dist asset is generated at build time (`muse-native build .`);
    // absent here means the recipe leg above owns the failure, not this one.
    if (!existsSync(dist)) {
      console.log('  detail: dist asset not generated in this checkout; skip');
      return;
    }
    if (existsSync(staged)) {
      assert.equal(
        sha256(staged),
        sha256(dist),
        'STUB_ROUTE_UNRESOLVABLE(tree-asset): assets/tree.mui.bin != ' +
          'dist/tree.mui.bin (stale copy packed)',
      );
    }
  });
});
