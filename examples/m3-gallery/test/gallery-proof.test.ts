'use strict';
// Batch 2 (T3/T4) gallery-proof hermetic halves — AC-M3-020 (FR-M3-009); TC-M3-021.
// T3: automated `gallery-proof` device command (install, page, assert counts +
//     no-blank, save screenshots). T4: build-hygiene companion (stale png,
//     gitignore, dist→assets fold; asset-sync step shared with T3).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. NEVER
// touches a device or emulator (no adb spawn, no ANDROID_SERIAL use), NEVER
// reads secrets or application config, NEVER downloads anything. All device
// behavior is asserted statically (source text contains the error strings and
// wiring) or against in-memory synthetic fixtures through pure helpers. No
// shared mutable state.
//
// Binds ONLY to the plan interfaces:
//   T3: `runGalleryProof({ serial, androidJar, outDir }): Promise<{ rows,
//       pages, shots[] }>` rejects counts-mismatch/blank-viewport/
//       device-unreachable; CLI `muse-native gallery-proof --tiers --themes
//       --assert-counts --no-blank [--out <dir>]`.
//   T4: `make install` copies current `dist/tree.mui.bin` into `assets/`;
//       none otherwise (build hygiene only).
// Never binds to an implementation.
//
// ASSUMPTIONS (contract genuinely silent on helper names — coder reconciles,
// per GR-08 T1.9; behavior pinned, spelling reconcilable):
//   A-H1: pure helpers are exported from `packages/cli/lib/gallery-proof.js`
//       as `deriveExpectedCounts(manifest)`, `isBlankViewport(frame)`,
//       `parseGalleryProofArgs(argv)`, `resolveAssetSyncPaths(opts)`.
//       If the coder nests them differently, call sites reconcile; the
//       expected behavior below is what must hold.
//   A-H2: `isBlankViewport` takes a frame object `{ width, height, pixels }`
//       where `pixels` is a Buffer/Uint8Array of RGBA bytes; a frame whose
//       pixels are all identical (zero variance) is blank.
//   A-H3: `deriveExpectedCounts` derives from the manifest object via the
//       same expansion as `buildStories(manifest)` (one story row per
//       component/variant/state × theme); exact stories.js import is the
//       coder's choice.
//   A-H4: minimum non-blank signal is 2 distinct byte values in the frame;
//       a defensible floor — the plan pins no threshold.
//   A-H5: default screenshot dir is `examples/m3-gallery/build/gallery-proof/`
//       relative to the repo root; `--out <dir>` overrides it.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join, resolve } = require('node:path');

const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const PROOF_LIB = join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-proof.js');
const CLI_BIN = join(REPO_ROOT, 'packages', 'cli', 'bin', 'muse-native.js');
const CLI_LIB = join(REPO_ROOT, 'packages', 'cli', 'lib', 'cli.js');
const MAKEFILE = join(__dirname, '..', 'Makefile');
const GITIGNORE = join(REPO_ROOT, '.gitignore');
const DEFAULT_OUT = join('examples', 'm3-gallery', 'build', 'gallery-proof');

function tryLoadProof() {
  try {
    return { mod: require(PROOF_LIB), error: null };
  } catch (err) {
    return { mod: null, error: err };
  }
}

function readText(path) {
  return readFileSync(path, 'utf8');
}

// Synthetic manifest fixtures (in-memory; never the real config, never disk).
function syntheticManifest() {
  return {
    name: 'test-fixture-not-real',
    components: [
      {
        name: 'Button',
        tier: 1,
        props: {
          variant: { type: 'string', values: ['filled', 'outlined'] },
          enabled: { type: 'boolean' },
        },
      },
      {
        name: 'Card',
        tier: 2,
        props: {
          tone: { type: 'token' },
        },
      },
    ],
  };
}

function blankFrame() {
  const pixels = Buffer.alloc(64 * 64 * 4, 0x80);
  return { width: 64, height: 64, pixels };
}

function nonBlankFrame() {
  const pixels = Buffer.alloc(64 * 64 * 4, 0x80);
  for (let i = 0; i < 256; i++) pixels[i * 4] = i % 256;
  return { width: 64, height: 64, pixels };
}

describe('T3 gallery-proof module contract', () => {
  it('exports runGalleryProof({ serial, androidJar, outDir })', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.runGalleryProof, 'function', 'runGalleryProof must be a function');
  });

  it('derives expected counts from the manifest, never hardcoded 147/12', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.deriveExpectedCounts, 'function', 'deriveExpectedCounts must be exported (A-H1)');
    const a = mod.deriveExpectedCounts(syntheticManifest());
    assert.ok(Number.isInteger(a.rows) && a.rows > 0, 'rows must be a positive integer');
    assert.ok(Number.isInteger(a.pages) && a.pages > 0, 'pages must be a positive integer');
    // Manifest sensitivity: removing a component must shrink the expectation.
    const trimmed = syntheticManifest();
    trimmed.components = trimmed.components.slice(0, 1);
    const b = mod.deriveExpectedCounts(trimmed);
    assert.ok(b.rows < a.rows, `counts must be manifest-derived (full=${a.rows} trimmed=${b.rows}), not hardcoded`);
    assert.ok(a.rows !== 147 && a.pages !== 12, 'must not hardcode Huawei rows=147/pages=12');
  });

  it('source carries no hardcoded Huawei rows/pages', () => {
    assert.ok(existsSync(PROOF_LIB), 'behavior missing: gallery-proof.js not implemented');
    const src = readText(PROOF_LIB);
    assert.ok(!/\brows\s*=\s*147\b/.test(src), 'must not hardcode rows=147');
    assert.ok(!/\bpages\s*=\s*12\b/.test(src), 'must not hardcode pages=12');
    assert.ok(/buildStories|manifest/i.test(src), 'counts must derive from buildStories(manifest)');
  });

  it('rejects counts-mismatch on expectation divergence', () => {
    assert.ok(existsSync(PROOF_LIB), 'behavior missing: gallery-proof.js not implemented');
    assert.ok(readText(PROOF_LIB).includes('counts-mismatch'), 'must reject with counts-mismatch');
  });

  it('rejects blank-viewport for blank frames, accepts real frames', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.isBlankViewport, 'function', 'isBlankViewport must be exported (A-H1/A-H2)');
    assert.equal(mod.isBlankViewport(blankFrame()), true, 'uniform frame must be blank');
    assert.equal(mod.isBlankViewport(nonBlankFrame()), false, 'varied frame must not be blank');
  });

  it('rejects diagnostic-only single-value viewport fixture', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const diag = { width: 32, height: 32, pixels: Buffer.alloc(32 * 32 * 4, 0x00) };
    assert.equal(mod.isBlankViewport(diag), true, 'diagnostic-only (all-zero) viewport must be blank');
    assert.ok(readText(PROOF_LIB).includes('blank-viewport'), 'must reject with blank-viewport');
  });

  it('rejects device-unreachable when no serial/device', () => {
    assert.ok(existsSync(PROOF_LIB), 'behavior missing: gallery-proof.js not implemented');
    const src = readText(PROOF_LIB);
    assert.ok(src.includes('device-unreachable'), 'must reject with device-unreachable');
    assert.ok(/serial/i.test(src), 'must take the device serial into account');
  });

  it('parses gallery-proof args with defaults', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.parseGalleryProofArgs, 'function', 'parseGalleryProofArgs must be exported (A-H1)');
    const full = mod.parseGalleryProofArgs([
      'gallery-proof', '--tiers', '--themes', '--assert-counts', '--no-blank', '--out', 'out/dir',
    ]);
    assert.equal(full.tiers, true);
    assert.equal(full.themes, true);
    assert.equal(full.assertCounts, true);
    assert.equal(full.noBlank, true);
    assert.equal(full.outDir, 'out/dir');
    const def = mod.parseGalleryProofArgs(['gallery-proof', '--tiers', '--themes', '--assert-counts', '--no-blank']);
    assert.ok(def.outDir.replace(/\\/g, '/').endsWith('examples/m3-gallery/build/gallery-proof'), `default outDir must be build/gallery-proof (got ${def.outDir})`);
  });

  it('CLI exposes muse-native gallery-proof with the proof flags', () => {
    const haystacks = [CLI_BIN, CLI_LIB].filter(existsSync).map(readText).join('\n');
    assert.ok(/gallery-proof/.test(haystacks), 'CLI must expose the gallery-proof command');
    for (const flag of ['--tiers', '--themes', '--assert-counts', '--no-blank']) {
      assert.ok(haystacks.includes(flag), `CLI must support ${flag}`);
    }
  });

  it('pages via nativeAdvancePage and verifies wrap on the emulator count', () => {
    assert.ok(existsSync(PROOF_LIB), 'behavior missing: gallery-proof.js not implemented');
    const src = readText(PROOF_LIB);
    assert.ok(/nativeAdvancePage/.test(src), 'must page via nativeAdvancePage');
    assert.ok(/wrap/i.test(src), 'must verify wrap on the emulator page count, not assume 12→1');
  });

  it('saves one screenshot per page under build/gallery-proof/', () => {
    assert.ok(existsSync(PROOF_LIB), 'behavior missing: gallery-proof.js not implemented');
    const src = readText(PROOF_LIB);
    assert.ok(/gallery-proof/.test(src), 'screenshots must land under build/gallery-proof/');
    assert.ok(/shots/.test(src), 'runGalleryProof must resolve { rows, pages, shots[] }');
  });

  it('records Vulkan/Swappy translator limits without forcing GPU paths', () => {
    assert.ok(existsSync(PROOF_LIB), 'behavior missing: gallery-proof.js not implemented');
    const src = readText(PROOF_LIB);
    assert.ok(/Swappy|Vulkan|translator/i.test(src), 'must record Vulkan/Swappy translator limits as emulator limits');
    assert.ok(!/--force-gpu|forceGpu|FORCE_GPU/.test(src), 'must not force GPU paths');
  });
});

describe('T4 build-hygiene companion', () => {
  it('asset-sync resolves current dist/tree.mui.bin into assets/', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: asset-sync step in gallery-proof.js not implemented');
    assert.equal(typeof mod.resolveAssetSyncPaths, 'function', 'resolveAssetSyncPaths must be exported (A-H1)');
    const paths = mod.resolveAssetSyncPaths({ galleryDir: join(REPO_ROOT, 'examples', 'm3-gallery') });
    const joined = `${paths.src} ${paths.dest}`.replace(/\\/g, '/');
    assert.ok(joined.includes('dist/tree.mui.bin'), `sync src must be dist/tree.mui.bin (got ${paths.src})`);
    assert.ok(joined.includes('assets/'), `sync dest must be under assets/ (got ${paths.dest})`);
  });

  it('make install folds the dist→assets copy so the APK never goes stale', () => {
    assert.ok(existsSync(MAKEFILE), 'examples/m3-gallery/Makefile missing');
    const src = readText(MAKEFILE);
    assert.ok(/tree\.mui\.bin/.test(src), 'Makefile must sync tree.mui.bin (no manual cp)');
    assert.ok(/^install:/m.test(src), 'Makefile must keep the install target');
  });

  it('gitignore keeps node_modules and silences build/apks/dist noise', () => {
    assert.ok(existsSync(GITIGNORE), '.gitignore missing');
    const lines = readText(GITIGNORE).split('\n').map((l) => l.trim()).filter(Boolean);
    assert.ok(lines.includes('node_modules/'), 'must keep node_modules/');
    for (const pat of ['build/', '*.apk', 'dist/']) {
      const covered = lines.some((l) => l === pat || l === `${pat}**/` || (pat === 'build/' && l.startsWith('build/')));
      assert.ok(covered, `.gitignore must silence ${pat}`);
    }
  });

  it('stale root m3-gallery-proof.png is gone or regenerated from the run', () => {
    const stale = join(REPO_ROOT, 'm3-gallery-proof.png');
    const referenced = ['docs', 'README.md', 'manual-context.local']
      .map((p) => join(REPO_ROOT, p))
      .filter(existsSync)
      .flatMap((p) => {
        try { return [readText(p)]; } catch { return []; }
      })
      .join('\n')
      .includes('m3-gallery-proof.png');
    assert.ok(!existsSync(stale) || referenced, 'stale root m3-gallery-proof.png must be deleted, or regenerated only if still referenced');
  });
});
