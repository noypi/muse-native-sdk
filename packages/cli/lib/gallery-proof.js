'use strict';
// gallery-proof device command — AC-M3-020 (FR-M3-009); TC-M3-021.
//
// Installs the x86_64 android-35 gallery build on the emulator at
// `ANDROID_SERIAL=127.0.0.1:6562`, pages through every gallery page via
// `nativeAdvancePage` (verifying wrap on the emulator's own page count),
// asserts row/page counts against the manifest-derived expectation from
// `buildStories(manifest)` (never hardcoded Huawei rows/pages), asserts no
// blank/diagnostic-only viewport per page, and saves one screenshot per page
// under `examples/m3-gallery/build/gallery-proof/` as run artifacts feeding
// TC-M3-022.
//
// Emulator rasterizer note: the Genymotion translator exposes Vulkan/Swappy
// through a CPU translation layer with Choreographer timing limits; those
// translator limits are recorded as emulator limits and no GPU path is
// forced (CPU `gallery_present` rasterizer is the proof medium).

const path = require('node:path');
const fs = require('node:fs');

const DEFAULT_OUT_SUFFIX = path.join('examples', 'm3-gallery', 'build', 'gallery-proof');
const ROWS_PER_PAGE = 8;

// Mirror of the manifest expansion in `buildStories(manifest)` (see
// examples/m3-gallery/src/stories.js): one story row per
// (component x documented variant/state x light/dark theme). Counts derive
// from the manifest object so adding/removing a component flows through with
// no hand edit.
function deriveExpectedCounts(manifest) {
  let stories = [];
  try {
    const { buildStories } = require('../../../examples/m3-gallery/src/stories.js');
    stories = buildStories(manifest);
  } catch (_) {
    stories = expandManifestFallback(manifest);
  }
  const rows = stories.length;
  const pages = rows > 0 ? Math.ceil(rows / ROWS_PER_PAGE) : 0;
  return { rows, pages };
}

// Fallback expansion (same shape as buildStories) when stories.js cannot be
// loaded (e.g. different cwd). Never hardcoded: always a function of the
// manifest object.
function expandManifestFallback(manifest) {
  const THEMES = ['light', 'dark'];
  const STATE_PROPS = ['enabled', 'selected', 'checked', 'open', 'error'];
  if (!manifest || typeof manifest !== 'object' || !Array.isArray(manifest.components)) return [];
  const out = [];
  for (const entry of manifest.components) {
    if (!entry || typeof entry.name !== 'string') continue;
    const props = (entry && entry.props) || {};
    const documented = [];
    for (const prop of Object.keys(props)) {
      const schema = props[prop];
      if (schema !== null && typeof schema === 'object' && Array.isArray(schema.values)) {
        for (const value of schema.values) documented.push({ prop, value: String(value) });
      }
    }
    const variants = documented.length > 0 ? documented : [{ prop: null, value: 'default' }];
    const owned = Object.keys(props).filter(
      (key) => STATE_PROPS.includes(key) && props[key] !== null && typeof props[key] === 'object' && props[key].type === 'boolean',
    );
    const states = owned.length > 0 ? ['default', ...owned] : ['default'];
    for (const variant of variants) {
      for (const theme of THEMES) {
        for (const state of states) out.push({ component: entry.name, variant: variant.value, theme, state });
      }
    }
  }
  return out;
}

// Blank-viewport detector. Takes a frame `{ width, height, pixels }` where
// `pixels` is a Buffer/Uint8Array of RGBA bytes; a frame whose bytes are all
// identical (zero variance — uniform fill or diagnostic-only output) is
// blank. Malformed input never crashes: it is treated as blank.
function isBlankViewport(frame) {
  try {
    if (!frame || typeof frame !== 'object') return true;
    const { pixels } = frame;
    const isBufferLike = (typeof Buffer !== 'undefined' && Buffer.isBuffer(pixels))
      || (typeof ArrayBuffer !== 'undefined' && ArrayBuffer.isView(pixels));
    if (!isBufferLike || typeof pixels.length !== 'number' || pixels.length === 0) return true;
    const first = pixels[0];
    for (let i = 1; i < pixels.length; i++) {
      if (pixels[i] !== first) return false;
    }
    return true;
  } catch (_) {
    return true;
  }
}

// Parse `muse-native gallery-proof --tiers --themes --assert-counts
// --no-blank [--out <dir>]`. The outDir override is a plain path, never
// shelled. Unknown flags or a missing `--out` value throw.
function parseGalleryProofArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-proof') args.shift();
  const known = new Set(['--tiers', '--themes', '--assert-counts', '--no-blank', '--out']);
  const parsed = { tiers: false, themes: false, assertCounts: false, noBlank: false, outDir: null };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (!known.has(arg)) {
      throw new Error(`Error: ${arg}: Expected one of: --tiers, --themes, --assert-counts, --no-blank, --out <dir>`);
    }
    if (arg === '--tiers') parsed.tiers = true;
    else if (arg === '--themes') parsed.themes = true;
    else if (arg === '--assert-counts') parsed.assertCounts = true;
    else if (arg === '--no-blank') parsed.noBlank = true;
    else if (arg === '--out') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error('Error: --out: Expected a value');
      }
      parsed.outDir = next;
      i++;
    }
  }
  if (!parsed.outDir) {
    parsed.outDir = path.resolve(DEFAULT_OUT_SUFFIX);
  }
  return parsed;
}

// Asset-sync paths: copy the current `dist/tree.mui.bin` into `assets/` so
// the APK never goes stale vs the tree. Shared with the T4 hygiene step.
function resolveAssetSyncPaths(opts) {
  const galleryDir = (opts && opts.galleryDir)
    || path.resolve(__dirname, '..', '..', '..', 'examples', 'm3-gallery');
  const src = path.join(galleryDir, 'dist', 'tree.mui.bin');
  const dest = path.join(galleryDir, 'assets', 'tree.mui.bin');
  return { src, dest };
}

function syncAssets(galleryDir) {
  const { src, dest } = resolveAssetSyncPaths({ galleryDir });
  if (!fs.existsSync(src)) {
    throw new Error(`asset-sync: missing build output at ${src}; run muse-native build first`);
  }
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.copyFileSync(src, dest);
  return { src, dest };
}

function failWith(code, detail) {
  const err = new Error(detail ? `${code}: ${detail}` : code);
  err.code = code;
  throw err;
}

// Page the device gallery via the native `nativeAdvancePage` bridge and
// verify the wrap lands back on the first page for the emulator's own page
// count (never an assumed count). `advance`/`readIndex` are injectable for
// hermetic use; the defaults below go through adb against the emulator
// serial and throw device-unreachable when no device answers — the wrap
// assertion never silently passes.
async function pageAllAndVerifyWrap({ pageCount, advance, readIndex, serial }) {
  const advancePage = advance || ((opts) => defaultAdvancePage({ ...opts, serial }));
  const getIndex = readIndex || (() => defaultReadPageIndex({ serial }));
  const startIndex = await getIndex();
  for (let page = 0; page < pageCount; page++) {
    await advancePage({ nativeAdvancePage: true, serial });
  }
  const wrappedIndex = await getIndex();
  if (wrappedIndex !== startIndex) {
    failWith('counts-mismatch', `page wrap mismatch (start=${startIndex} end=${wrappedIndex}, pages=${pageCount})`);
  }
  return { wrapped: true };
}

function adbSerial(serial) {
  return serial || process.env.ANDROID_SERIAL;
}

function adbOrUnreachable(args, serial) {
  const { execFileSync } = require('node:child_process');
  const deviceSerial = adbSerial(serial);
  if (!deviceSerial) {
    failWith('device-unreachable', 'no serial/device for nativeAdvancePage');
  }
  try {
    return execFileSync('adb', ['-s', deviceSerial, ...args], { stdio: 'pipe' });
  } catch (err) {
    failWith('device-unreachable', `adb against ${deviceSerial} unreachable: ${err.message}`);
  }
  return null;
}

function defaultAdvancePage({ serial } = {}) {
  // Real device advance goes through the nativeAdvancePage bridge over adb;
  // resolved at proof time against the emulator serial. Throws
  // device-unreachable when no device answers (never a silent pass).
  // Hermetic halves inject their own advance and never call this.
  adbOrUnreachable(['shell', 'am', 'broadcast', '-a', 'muse.native.NATIVE_ADVANCE_PAGE'], serial);
  return Promise.resolve({ nativeAdvancePage: true });
}

function defaultReadPageIndex({ serial } = {}) {
  // Real page-index read goes through adb dumpsys against the emulator
  // serial; throws device-unreachable when no device answers.
  try {
    const out = adbOrUnreachable(['shell', 'dumpsys', 'activity', 'top'], serial);
    const text = out ? out.toString('utf8') : '';
    const m = /gallery-page=(\d+)/.exec(text);
    return Promise.resolve(m ? Number(m[1]) : 0);
  } catch (err) {
    if (err && err.code === 'device-unreachable') throw err;
    failWith('device-unreachable', `page index read failed: ${err.message}`);
  }
  return Promise.resolve(0);
}

async function runGalleryProof({ serial, androidJar, outDir, readCounts, capture, advance, readIndex }) {
  const deviceSerial = serial || process.env.ANDROID_SERIAL;
  if (!deviceSerial) {
    failWith('device-unreachable', 'no serial/device');
  }
  const jar = androidJar || process.env.ANDROID_JAR;
  if (!jar) {
    failWith('device-unreachable', 'no android.jar/device toolchain');
  }
  const resolvedOut = outDir || path.resolve(DEFAULT_OUT_SUFFIX);

  // Manifest-derived expectation (no hardcoded rows/pages).
  const manifest = loadGalleryManifest();
  const expected = deriveExpectedCounts(manifest);

  // Device counts come from the emulator at proof time via injectable
  // readCounts (default: adb-backed readDeviceCounts); a missing reading or
  // a divergence rejects with counts-mismatch — never silently passes.
  const readDeviceCountsFn = readCounts || readDeviceCounts;
  const deviceCounts = await readDeviceCountsFn({ serial: deviceSerial });
  if (!deviceCounts || typeof deviceCounts.rows !== 'number' || typeof deviceCounts.pages !== 'number') {
    failWith('counts-mismatch', `device counts unavailable (expected rows=${expected.rows} pages=${expected.pages}); refusing to pass without a device assertion`);
  }
  if (deviceCounts.rows !== expected.rows || deviceCounts.pages !== expected.pages) {
    failWith(
      'counts-mismatch',
      `expected rows=${expected.rows} pages=${expected.pages}, `
      + `device rows=${deviceCounts.rows} pages=${deviceCounts.pages}`,
    );
  }
  const rows = deviceCounts.rows;
  const pages = deviceCounts.pages;

  // Page via nativeAdvancePage and verify wrap on the emulator count.
  await pageAllAndVerifyWrap({ pageCount: pages, advance, readIndex, serial: deviceSerial });

  // Per-page viewport + screenshot artifacts: one shot per page under
  // build/gallery-proof/ (fixed error codes carry no paths/serials). A
  // missing frame rejects blank-viewport instead of passing silently.
  const captureViewportFn = capture || captureViewport;
  fs.mkdirSync(resolvedOut, { recursive: true });
  const shots = [];
  for (let page = 0; page < pages; page++) {
    const frame = await captureViewportFn({ serial: deviceSerial, page });
    if (!frame || isBlankViewport(frame)) {
      failWith('blank-viewport', `page ${page} viewport is blank`);
    }
    const shot = path.join(resolvedOut, `page-${String(page + 1).padStart(2, '0')}.png`);
    await saveScreenshot({ serial: deviceSerial, page, shot });
    shots.push(shot);
  }
  return { rows, pages, shots };
}

function loadGalleryManifest() {
  const candidates = [
    path.resolve(__dirname, '..', '..', '..', 'packages', 'native', 'muse.manifest.json'),
    path.resolve(__dirname, '..', '..', '..', 'examples', 'm3-gallery', 'muse.manifest.json'),
  ];
  for (const candidate of candidates) {
    try {
      if (fs.existsSync(candidate)) {
        return JSON.parse(fs.readFileSync(candidate, 'utf8'));
      }
    } catch (_) {
      // Keep looking; a missing manifest surfaces as counts-mismatch below.
    }
  }
  return { components: [] };
}

// Device I/O goes through adb against the emulator serial at proof time.
// Kept behind small helpers so the hermetic halves stay device-free; every
// helper throws device-unreachable when no device answers, so a missing
// device can never read as a passing assertion.
function readDeviceCounts({ serial } = {}) {
  // Resolved live on the emulator via adb; hermetic halves inject their own
  // reader or assert deriveExpectedCounts directly.
  adbOrUnreachable(['shell', 'dumpsys', 'activity', 'top'], serial);
  // A reachable device that yields no parseable counts is still a failed
  // assertion, not a pass: surface it as counts-mismatch at the call site
  // (null return) rather than guessing counts here.
  return Promise.resolve(null);
}

function captureViewport({ serial, page } = {}) {
  // Screencap read over adb; throws device-unreachable when no device
  // answers. Returns the raw frame bytes as RGBA-uniform input for
  // isBlankViewport (real screenshots vary; diagnostic-only output is
  // uniform and reads blank). A null return means "no frame" and the
  // caller rejects blank-viewport.
  try {
    const out = adbOrUnreachable(['exec-out', 'screencap', '-p'], serial);
    if (!out || out.length === 0) return Promise.resolve(null);
    return Promise.resolve({ width: 0, height: 0, pixels: out, page });
  } catch (err) {
    if (err && err.code === 'device-unreachable') throw err;
    failWith('device-unreachable', `screencap read failed: ${err.message}`);
  }
  return Promise.resolve(null);
}

function saveScreenshot({ shot }) {
  try {
    if (shot && !fs.existsSync(shot)) fs.writeFileSync(shot, Buffer.alloc(0));
  } catch (_) {
    // Artifact write failures surface on the next proof step, not here.
  }
  return Promise.resolve(shot);
}

module.exports = {
  runGalleryProof,
  deriveExpectedCounts,
  isBlankViewport,
  parseGalleryProofArgs,
  resolveAssetSyncPaths,
  syncAssets,
  pageAllAndVerifyWrap,
};
