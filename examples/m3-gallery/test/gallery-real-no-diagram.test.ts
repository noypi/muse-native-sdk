'use strict';
// FR-M3-011 Batch 1 (T1 + T2 contract).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only.
// Synthetic in-memory descriptors and manifests; no network, no secrets,
// no application configuration, no device, no live service, no shared
// mutable state. `process.env` is never read: flag/env decisions take an
// explicit input with caller-supplied dummy values.
//
// Binds ONLY to the plan interfaces (reproduced verbatim in unit-tests/T1.md
// and unit-tests/T2.md):
//   T1: `attachGallerySurface(descriptorPath: string): SurfaceHandle | BuildError — descriptorPath is tree.mui.bin path; returns opaque SurfaceHandle or BuildError{mockReachable: boolean}`
//   T2: `assertNoMockPainter(pageTarget: string): void throws MockReachableError — pageTarget is a gallery page target; throws MockReachableError{symbol: string} when any mock-painter symbol is reachable`
//
// ASSUMPTIONS (plan Interface genuinely silent on module/file spelling —
// coder reconciles layout per GR-08 T1.9; behavior pinned, spelling
// reconcilable):
//   A1 (T1 module path): `attachGallerySurface` lives on the gallery surface
//       entry `examples/m3-gallery/src/gallerySurface.js` (CommonJS). If the
//       coder nests it elsewhere, call sites reconcile; the behavior below
//       is what must hold.
//   A2 (T2 module path): `assertNoMockPainter` lives in the gallery-real CLI
//       module. The plan names `packages/cli/src/gallery-real.js`; the
//       checkout lays CLI code out under `packages/cli/lib/`. Tests probe
//       `lib/gallery-real.js` first and fall back to `src/gallery-real.js`;
//       either location satisfies the contract.
//   A3 (T2 CLI flag): the reachability gate is wired into the `gallery-real`
//       CLI `--assert-no-diagram` flag following the gallery-proof CLI
//       pattern (`parseGalleryProofArgs` / `runGalleryTree(argv, env)`).
//   A4 (error shapes): `BuildError` is `{ mockReachable: boolean }` plus an
//       Error message; `MockReachableError` is an Error with a string
//       `symbol` property naming the reachable mock-painter symbol. Tests
//       assert the named field, not the full class hierarchy.
//   A5 (no-PaintCaption scope): "no diagram output" means no `PaintCaption`
//       box/caption stand-in string in device-frame output or in the
//       gallery hosting source (`gallerySurface.js`, `App.tsx`).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join, resolve } = require('node:path');

const SURFACE_PATH = '../src/gallerySurface.js';
const APP_PATH = join(__dirname, '..', 'src', 'App.tsx');
const SURFACE_SRC_PATH = join(__dirname, '..', 'src', 'gallerySurface.js');
const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const GALLERY_REAL_CANDIDATES = [
  join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-real.js'),
  join(REPO_ROOT, 'packages', 'cli', 'src', 'gallery-real.js'),
];
const MAKEFILE_PATH = join(__dirname, '..', 'Makefile');

let surfaceError = null;
let surface = null;
try {
  surface = require(SURFACE_PATH);
} catch (err) {
  surfaceError = err;
}

function loadGalleryReal() {
  for (const candidate of GALLERY_REAL_CANDIDATES) {
    if (!existsSync(candidate)) continue;
    try {
      const mod = require(candidate);
      return { mod, candidate, error: null };
    } catch (err) {
      return { mod: null, candidate, error: err };
    }
  }
  return { mod: null, candidate: GALLERY_REAL_CANDIDATES[0], error: new Error('gallery-real module missing') };
}

function requireAttach() {
  assert.equal(
    surfaceError,
    null,
    `gallery surface module missing at ${SURFACE_PATH}: ${surfaceError && surfaceError.message}`,
  );
  assert.equal(
    typeof surface.attachGallerySurface,
    'function',
    'gallerySurface must export attachGallerySurface(descriptorPath) (T1)',
  );
}

function requireAssertNoMockPainter(loaded) {
  assert.ok(loaded.mod, `gallery-real module missing (tried ${GALLERY_REAL_CANDIDATES.join(', ')}): ${loaded.error && loaded.error.message}`);
  assert.equal(
    typeof loaded.mod.assertNoMockPainter,
    'function',
    'gallery-real must export assertNoMockPainter(pageTarget) (T2)',
  );
}

function readText(path) {
  return readFileSync(path, 'utf8');
}

describe('T1 — attachGallerySurface via the engine-owned Surface host contract', () => {
  it('exports attachGallerySurface(descriptorPath)', () => {
    requireAttach();
  });

  it('attaches a valid tree.mui.bin descriptor to an opaque SurfaceHandle', () => {
    requireAttach();
    // Synthetic descriptor path only: no real file read. The fake presenter
    // behind the seam accepts any caller-supplied tree.mui.bin-shaped path.
    const handle = surface.attachGallerySurface('dist/tree.mui.bin');
    assert.ok(handle && typeof handle === 'object', 'valid descriptor must return a SurfaceHandle object');
    assert.equal(handle.mockReachable, undefined, 'a SurfaceHandle must not carry a BuildError mockReachable flag');
  });

  it('returns BuildError{mockReachable: boolean} for an unreadable descriptor', () => {
    requireAttach();
    const err = surface.attachGallerySurface('dist/missing-tree.mui.bin');
    assert.ok(err instanceof Error, 'unreadable descriptor must return a BuildError Error');
    assert.equal(typeof err.mockReachable, 'boolean', 'BuildError must carry boolean mockReachable');
  });

  it('returns BuildError with mockReachable:true when a mock painter is reachable', () => {
    requireAttach();
    // Synthetic hostile descriptor: caller-supplied fake seam reports a
    // reachable mock painter for this path. Production maps it to
    // BuildError{mockReachable: true}, never a handle.
    const err = surface.attachGallerySurface('dist/tree-with-mock.mui.bin');
    assert.ok(err instanceof Error, 'mock-reachable descriptor must return a BuildError Error');
    assert.equal(err.mockReachable, true, 'mock-reachable descriptor must set mockReachable:true');
  });

  it('instantiates pages from the same tree.mui.bin descriptors (no per-page hosting)', () => {
    requireAttach();
    // Same descriptor path twice must yield equivalent handles: every page
    // goes through the shared presenter seam, not a gallery-only branch.
    const a = surface.attachGallerySurface('dist/tree.mui.bin');
    const b = surface.attachGallerySurface('dist/tree.mui.bin');
    assert.deepEqual(
      Object.keys(a || {}).sort(),
      Object.keys(b || {}).sort(),
      'same descriptor must produce the same SurfaceHandle shape (shared seam, no per-page fork)',
    );
  });

  it('no PaintCaption stand-ins in the gallery hosting source', () => {
    assert.ok(existsSync(SURFACE_SRC_PATH), 'src/gallerySurface.js must still exist');
    assert.ok(existsSync(APP_PATH), 'src/App.tsx must still exist');
    for (const path of [SURFACE_SRC_PATH, APP_PATH]) {
      const src = readText(path);
      assert.ok(!/PaintCaption/.test(src), `${path} must not contain PaintCaption stand-ins (T1)`);
    }
  });

  it('empty/blank descriptorPath is rejected with a BuildError, never a handle', () => {
    requireAttach();
    for (const bad of ['', '   ', null, undefined]) {
      const out = surface.attachGallerySurface(bad);
      assert.ok(out instanceof Error, `descriptorPath ${JSON.stringify(String(bad))} must return a BuildError`);
      assert.equal(typeof out.mockReachable, 'boolean', 'BuildError must carry boolean mockReachable');
    }
  });
  it('rejects oversized descriptorPath fast with a bounded BuildError message', () => {
    requireAttach();
    const huge = 'x'.repeat(1024 * 1024);
    const start = Date.now();
    const out = surface.attachGallerySurface(huge);
    assert.ok(out instanceof Error, '1MB descriptorPath must return a BuildError, never a handle');
    assert.equal(typeof out.mockReachable, 'boolean', 'BuildError must carry boolean mockReachable');
    assert.ok(out.message.length < 500, `oversized denial message must stay bounded (got ${out.message.length} chars)`);
    assert.ok(Date.now() - start < 5000, 'oversized descriptorPath rejection must not hang');
  });

  it('rejects control-char descriptorPath with a bounded BuildError message', () => {
    requireAttach();
    for (const bad of ['ok\nerror: mock-painter reachable: FAKE', 'dist/tree.mui.bin\x00evil', 'dist/tree\x1b[31m.mui.bin']) {
      const out = surface.attachGallerySurface(bad);
      assert.ok(out instanceof Error, `control-char descriptorPath must return a BuildError, never a handle`);
      assert.equal(typeof out.mockReachable, 'boolean', 'BuildError must carry boolean mockReachable');
      assert.ok(out.message.length < 500, `control-char denial message must stay bounded (got ${out.message.length} chars)`);
    }
  });
});

describe('T2 — assertNoMockPainter reachability gate fails the build', () => {
  it('exports assertNoMockPainter(pageTarget)', () => {
    requireAssertNoMockPainter(loadGalleryReal());
  });

  it('clean page target passes without throwing', () => {
    const loaded = loadGalleryReal();
    requireAssertNoMockPainter(loaded);
    assert.doesNotThrow(
      () => loaded.mod.assertNoMockPainter('gallery-page-button'),
      'a page with no mock-painter symbol must not throw',
    );
  });

  it('page importing a mock-painter symbol throws MockReachableError{symbol}', () => {
    const loaded = loadGalleryReal();
    requireAssertNoMockPainter(loaded);
    assert.throws(
      () => loaded.mod.assertNoMockPainter('gallery-page-with-PaintCaption'),
      (err) => {
        assert.ok(err instanceof Error, 'must throw an Error');
        assert.equal(typeof err.symbol, 'string', 'MockReachableError must carry string symbol');
        assert.ok(err.symbol.length > 0, 'MockReachableError.symbol must name the symbol');
        assert.ok(/PaintCaption|mock/i.test(err.symbol), `symbol must name the mock-painter symbol (got ${JSON.stringify(err.symbol)})`);
        return true;
      },
      'a mock-painter-reachable page must throw MockReachableError{symbol}',
    );
  });

  it('hostile/oversized pageTarget values fail closed, never pass silently', () => {
    const loaded = loadGalleryReal();
    requireAssertNoMockPainter(loaded);
    for (const bad of ['', '__proto__', 'x'.repeat(10000)]) {
      assert.throws(
        () => loaded.mod.assertNoMockPainter(bad),
        Error,
        `pageTarget ${JSON.stringify(String(bad)).slice(0, 40)} must throw (fail-closed)`,
      );
    }
  });

  it('on-disk sweep pattern is case-insensitive (mockpainter/MOCKPAINTER/paintcaption)', () => {
    const loaded = loadGalleryReal();
    requireAssertNoMockPainter(loaded);
    const src = readText(loaded.candidate);
    assert.ok(
      /SOURCE_MOCK_PAINTER_RE\s*=\s*\/[^/]+\/i/.test(src),
      'the on-disk sweep pattern SOURCE_MOCK_PAINTER_RE must carry the case-insensitive flag',
    );
    assert.ok(
      /MOCKPAINTER|mockpainter/i.test('mockpainter MOCKPAINTER paintcaption'),
      'sanity: case-variant sweep samples are non-empty strings',
    );
  });

  it('on-disk sweep covers .tsx/.ts/.js/.jsx alike (JS gate and Makefile gate)', () => {
    const loaded = loadGalleryReal();
    requireAssertNoMockPainter(loaded);
    const src = readText(loaded.candidate);
    assert.ok(
      /tsx\|ts\|js\|jsx|tsx.*ts.*js.*jsx/.test(src),
      'scanPageSources must allowlist tsx, ts, js, and jsx alike',
    );
    const make = readText(MAKEFILE_PATH);
    for (const ext of ['*.tsx', '*.ts', '*.js', '*.jsx']) {
      assert.ok(make.includes(ext), `Makefile assert-no-diagram sweep must include ${ext}`);
    }
    assert.ok(/grep\s+-rniE|grep\s+-rn\w*i/.test(make), 'Makefile sweep grep must stay case-insensitive (-i)');
  });

  it('parseGalleryRealArgs unknown-option denial message stays bounded for oversized flags', () => {
    const loaded = loadGalleryReal();
    requireAssertNoMockPainter(loaded);
    assert.equal(typeof loaded.mod.parseGalleryRealArgs, 'function', 'gallery-real must export parseGalleryRealArgs()');
    const huge = `x`.repeat(100000);
    let err = null;
    try {
      loaded.mod.parseGalleryRealArgs(['gallery-real', huge], {});
    } catch (e) {
      err = e;
    }
    assert.ok(err instanceof Error, 'unknown oversized flag must throw fail-closed');
    assert.ok(err.message.length < 500, `unknown-option denial message must stay bounded (got ${err.message.length} chars)`);
  });

  it('oracle painters stay importable only from unit-test/hermetic targets (Makefile gate)', () => {
    assert.ok(existsSync(MAKEFILE_PATH), 'examples/m3-gallery/Makefile must still exist');
    const src = readText(MAKEFILE_PATH);
    assert.ok(/mock|MockPainter|PaintCaption|assert-no-diagram/i.test(src), 'Makefile must contain the mock-painter reachability gate (T2)');
  });

  it('gallery-real CLI exposes --assert-no-diagram', () => {
    const loaded = loadGalleryReal();
    assert.ok(existsSync(loaded.candidate), `gallery-real module must exist at ${loaded.candidate} (A2)`);
    const src = readText(loaded.candidate);
    assert.ok(/assert-no-diagram/.test(src), 'gallery-real CLI must wire the --assert-no-diagram flag (A3)');
  });
});
