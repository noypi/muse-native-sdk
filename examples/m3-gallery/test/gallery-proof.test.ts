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
const os = require('node:os');

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

// Hermetic runGalleryProof driver: everything device-shaped is injected.
// Serial/jar are obvious dummies; outDir lands under os.tmpdir() (never the
// repo); advance/readIndex are no-op page fakes (wrap holds: index stays 0).
// No adb is ever spawned: without injected readCounts/capture/advance the
// test would fail, so every behavior below injects all three. ANDROID_SERIAL
// / ANDROID_JAR are cleared per call so the result cannot depend on the host.
function proofDriver(mod, overrides = {}) {
  const savedSerial = process.env.ANDROID_SERIAL;
  const savedJar = process.env.ANDROID_JAR;
  delete process.env.ANDROID_SERIAL;
  delete process.env.ANDROID_JAR;
  const fs = require('node:fs');
  const outDir = fs.mkdtempSync(join(os.tmpdir(), 'gallery-proof-'));
  const restore = () => {
    if (savedSerial === undefined) delete process.env.ANDROID_SERIAL;
    else process.env.ANDROID_SERIAL = savedSerial;
    if (savedJar === undefined) delete process.env.ANDROID_JAR;
    else process.env.ANDROID_JAR = savedJar;
  };
  const base = {
    serial: 'test-serial-not-real',
    androidJar: 'test-android-jar-not-real',
    outDir,
    sync: async () => ({ src: 'test-sync-src-not-real', dest: 'test-sync-dest-not-real' }),
    advance: async () => ({ nativeAdvancePage: true }),
    readIndex: async () => 0,
  };
  const opts = { ...base, ...overrides };
  return { opts, restore };
}

async function rejectsWithCode(promise, code) {
  let err = null;
  try {
    await promise;
  } catch (e) {
    err = e;
  }
  assert.ok(err, `runGalleryProof must reject with ${code}`);
  const text = `${err.code || ''} ${err.message || ''}`;
  assert.ok(text.includes(code), `rejection must carry ${code} (got ${text})`);
  return err;
}

// Read the on-disk gallery manifest with the same candidate order as
// loadGalleryManifest() so a readCounts fake can echo the true expectation
// back (positive control) or diverge from it (mismatch control) without
// hardcoding rows/pages.
function loadManifestForExpectation() {
  const candidates = [
    join(REPO_ROOT, 'packages', 'native', 'muse.manifest.json'),
    join(REPO_ROOT, 'examples', 'm3-gallery', 'muse.manifest.json'),
  ];
  for (const candidate of candidates) {
    try {
      if (existsSync(candidate)) return JSON.parse(readFileSync(candidate, 'utf8'));
    } catch (_) { /* keep looking */ }
  }
  return { components: [] };
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

  it('rejects counts-mismatch on diverged device counts (injected readCounts)', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const expected = mod.deriveExpectedCounts(loadManifestForExpectation());
    const { opts, restore } = proofDriver(mod, {
      readCounts: async () => ({ rows: expected.rows + 1, pages: expected.pages }),
      capture: async () => nonBlankFrame(),
    });
    try {
      await rejectsWithCode(mod.runGalleryProof(opts), 'counts-mismatch');
    } finally {
      restore();
    }
  });

  it('rejects counts-mismatch on missing device counts (injected null)', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const { opts, restore } = proofDriver(mod, {
      readCounts: async () => null,
      capture: async () => nonBlankFrame(),
    });
    try {
      await rejectsWithCode(mod.runGalleryProof(opts), 'counts-mismatch');
    } finally {
      restore();
    }
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

  it('treats a malformed frame (non-Buffer pixels) as blank, never crashing', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.isBlankViewport, 'function', 'isBlankViewport must be exported (A-H1/A-H2)');
    assert.equal(mod.isBlankViewport({ width: 8, height: 8, pixels: 'not-bytes-not-real' }), true, 'string pixels must read blank');
    assert.equal(mod.isBlankViewport({ width: 8, height: 8 }), true, 'missing pixels must read blank');
    assert.equal(mod.isBlankViewport(null), true, 'null frame must read blank');
  });

  it('rejects device-unreachable when no serial/device (injected fakes only)', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const savedSerial = process.env.ANDROID_SERIAL;
    const savedJar = process.env.ANDROID_JAR;
    delete process.env.ANDROID_SERIAL;
    delete process.env.ANDROID_JAR;
    try {
      await rejectsWithCode(
        mod.runGalleryProof({
          serial: undefined,
          androidJar: 'test-android-jar-not-real',
          outDir: join(os.tmpdir(), 'gallery-proof-no-serial'),
          sync: async () => ({ src: 'test-sync-src-not-real', dest: 'test-sync-dest-not-real' }),
          readCounts: async () => ({ rows: 1, pages: 1 }),
          capture: async () => nonBlankFrame(),
          advance: async () => ({ nativeAdvancePage: true }),
          readIndex: async () => 0,
        }),
        'device-unreachable',
      );
    } finally {
      if (savedSerial === undefined) delete process.env.ANDROID_SERIAL;
      else process.env.ANDROID_SERIAL = savedSerial;
      if (savedJar === undefined) delete process.env.ANDROID_JAR;
      else process.env.ANDROID_JAR = savedJar;
    }
  });

  it('propagates device-unreachable from the injected advance', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const expected = mod.deriveExpectedCounts(loadManifestForExpectation());
    const deviceUnreachable = () => {
      const err = new Error('device-unreachable: test-injected');
      err.code = 'device-unreachable';
      throw err;
    };
    const { opts, restore } = proofDriver(mod, {
      readCounts: async () => ({ rows: expected.rows, pages: expected.pages }),
      capture: async () => nonBlankFrame(),
      advance: async () => deviceUnreachable(),
    });
    try {
      await rejectsWithCode(mod.runGalleryProof(opts), 'device-unreachable');
    } finally {
      restore();
    }
  });

  it('rejects blank-viewport for a blank frame via runGalleryProof (injected capture)', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const expected = mod.deriveExpectedCounts(loadManifestForExpectation());
    const { opts, restore } = proofDriver(mod, {
      readCounts: async () => ({ rows: expected.rows, pages: expected.pages }),
      capture: async () => blankFrame(),
    });
    try {
      await rejectsWithCode(mod.runGalleryProof(opts), 'blank-viewport');
    } finally {
      restore();
    }
  });

  it('rejects blank-viewport for a missing frame via runGalleryProof (injected null)', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const expected = mod.deriveExpectedCounts(loadManifestForExpectation());
    const { opts, restore } = proofDriver(mod, {
      readCounts: async () => ({ rows: expected.rows, pages: expected.pages }),
      capture: async () => null,
    });
    try {
      await rejectsWithCode(mod.runGalleryProof(opts), 'blank-viewport');
    } finally {
      restore();
    }
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

describe('T3 tree-oracle + viewport hermetic pins', () => {
  function fixtureTreeIR() {
    return {
      roots: [
        {
          file: 'src/Showcase.tsx',
          children: [
            { type: 'Button', props: { label: 'Ok', variant: 'filled' } },
            { type: 'Card', props: { title: 'Hello' } },
            { type: 'View', props: {} },
            { type: '#text', value: 'M3 Gallery light' },
            { type: '#text', value: 'Hello row' },
            { type: '#text', value: '...skip me' },
          ],
        },
      ],
    };
  }

  function mui1Bytes(ir, { magic = 'MUI1', version = 1, corruptJson = false } = {}) {
    const payload = corruptJson ? Buffer.from('{not-json', 'utf8') : Buffer.from(JSON.stringify(ir), 'utf8');
    const header = Buffer.alloc(12);
    header.write(magic, 0, 4, 'ascii');
    header.writeUInt32LE(version, 4);
    header.writeUInt32LE(payload.length, 8);
    return Buffer.concat([header, payload]);
  }

  function assertIoError(fn, label) {
    let err = null;
    try {
      fn();
    } catch (e) {
      err = e;
    }
    assert.ok(err, `${label} must throw io-error`);
    assert.equal(err.code, 'io-error', `${label} must carry io-error (got ${err && err.code})`);
  }

  it('parseTreeRows filters wrappers/non-alnum and orders components first', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.parseTreeRows, 'function', 'parseTreeRows must be exported');
    const rows = mod.parseTreeRows(fixtureTreeIR());
    assert.equal(rows.length, 4, `fixture must yield 4 rows (got ${rows.length})`);
    assert.deepEqual(rows.map((r) => r.comp), ['Button', 'Card', '', '']);
    assert.ok(!rows.some((r) => r.comp === 'View'), 'View wrapper must never become a row');
    assert.ok(!rows.some((r) => r.label === '...skip me'), 'non-alnum text must be skipped');
  });

  it('deriveTreeCounts over a fixture tree yields rows and paged pages', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.deriveTreeCounts, 'function', 'deriveTreeCounts must be exported');
    const ir = fixtureTreeIR();
    const tall = mod.deriveTreeCounts(ir, 3000);
    assert.equal(tall.rows, 4, 'fixture rows must be 4');
    assert.equal(tall.pages, 1, 'tall viewport must fit on one page');
    const short = mod.deriveTreeCounts(mui1Bytes(ir), 300);
    assert.equal(short.rows, 4, 'MUI1 bytes path must yield the same rows');
    assert.ok(short.pages >= 1, 'short viewport must yield >= 1 page');
    assert.ok(short.pages >= tall.pages, 'shorter viewport must not page less');
  });

  it('decodeTreeIR rejects bad magic/version/length/non-JSON with io-error', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    const ir = fixtureTreeIR();
    assertIoError(() => mod.decodeTreeIR(mui1Bytes(ir, { magic: 'BAD!' })), 'bad magic');
    assertIoError(() => mod.decodeTreeIR(mui1Bytes(ir, { version: 2 })), 'bad version');
    const good = mui1Bytes(ir);
    const truncated = good.subarray(0, good.length - 5);
    assertIoError(() => mod.decodeTreeIR(truncated), 'truncated length');
    assertIoError(() => mod.decodeTreeIR(Buffer.from('short', 'utf8')), 'short buffer');
    assertIoError(() => mod.decodeTreeIR(mui1Bytes(ir, { corruptJson: true })), 'non-JSON payload');
  });

  it('missingTreeComponents lists absent manifest components', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.missingTreeComponents, 'function', 'missingTreeComponents must be exported');
    const manifest = { components: [{ name: 'Button' }, { name: 'Card' }, { name: 'Slider' }] };
    const rows = [{ comp: 'Button', desc: 'Button', label: 'b' }];
    assert.deepEqual(mod.missingTreeComponents(manifest, rows), ['Card', 'Slider']);
    assert.deepEqual(mod.missingTreeComponents(manifest, [{ comp: 'Button' }, { comp: 'Card' }, { comp: 'Slider' }]), []);
    assert.deepEqual(mod.missingTreeComponents({ components: [{ name: 'View' }, { name: 'Text' }, { name: 'Button' }] }, []), ['Button']);
  });

  it('logcat viewport filter keeps both tags in valid tag-level form', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.logcatViewportFilterArgs, 'function', 'logcatViewportFilterArgs must be exported');
    const args = mod.logcatViewportFilterArgs();
    assert.ok(args.includes('GalleryPage:I'), 'must keep GalleryPage:I');
    assert.ok(args.some((a) => /^muse-host:I$/i.test(a)), 'must include muse-host:I so the attach marker survives *:S');
    assert.ok(args.includes('*:S'), 'must silence everything else');
    for (const a of args.slice(2)) {
      assert.ok((a.match(/:/g) || []).length <= 1, `filter spec must not embed a second colon (got ${a})`);
    }
  });

  it('defaultReadViewport retries an empty scan then resolves the attach marker height', async () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(typeof mod.defaultReadViewport, 'function', 'defaultReadViewport must be exported');
    const cp = require('node:child_process');
    const orig = cp.execFileSync;
    let calls = 0;
    cp.execFileSync = () => {
      calls++;
      if (calls <= 2) return Buffer.from('nothing yet', 'utf8');
      return Buffer.from('nativeAttachSurface: window 1080x2400', 'utf8');
    };
    try {
      const found = await mod.defaultReadViewport({ serial: 'test-serial-not-real', attempts: 4, sleepMs: 0, sleep: async () => {} });
      assert.equal(found.height, 2400, 'must resolve the attach marker height');
      assert.equal(found.width, 1080, 'must resolve the attach marker width');
      assert.ok(calls > 2, `must retry past the first empty scan (calls=${calls})`);
    } finally {
      cp.execFileSync = orig;
    }
  });

  it('malformed frames fail closed to blank, never throw (GAP reconciliation)', () => {
    const { mod } = tryLoadProof();
    assert.ok(mod, 'behavior missing: packages/cli/lib/gallery-proof.js not implemented');
    assert.equal(mod.isBlankViewport({ width: 8, height: 8, pixels: 'not-bytes-not-real' }), true);
    assert.equal(mod.isBlankViewport({ width: 8, height: 8 }), true);
    assert.equal(mod.isBlankViewport(null), true);
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
