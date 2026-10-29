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
//
// Deploy note (TC-M3-021 second-tester fix): the proof owns the device
// deploy — after the dist/tree.mui.bin → assets/ sync it adb-installs the
// x86_64 android-35 gallery APK and launches com.muse.host/.MainActivity,
// then waits for gallery markers before any dumpsys/logcat read. Install
// always follows the sync so the fresh tree reaches the installed APK.
// Logcat uses the valid tag-level filter form `GalleryPage:I` (logcat
// rejects a filter spec with an embedded second colon).

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
// --no-blank [--out <dir>] [--apk <path>]`. The outDir/apkPath overrides
// are plain paths, never shelled. Unknown flags or a missing `--out`/`--apk`
// value throw.
function parseGalleryProofArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-proof') args.shift();
  const known = new Set(['--tiers', '--themes', '--assert-counts', '--no-blank', '--out', '--apk']);
  const parsed = { tiers: false, themes: false, assertCounts: false, noBlank: false, outDir: null, apkPath: null };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (!known.has(arg)) {
      throw new Error(`Error: ${arg}: Expected one of: --tiers, --themes, --assert-counts, --no-blank, --out <dir>, --apk <path>`);
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
    } else if (arg === '--apk') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error('Error: --apk: Expected a value');
      }
      parsed.apkPath = next;
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
  try {
    fs.mkdirSync(path.dirname(dest), { recursive: true });
    fs.copyFileSync(src, dest);
  } catch (err) {
    if (err && (err.code === 'io-error' || /missing build output/.test(err.message || ''))) throw err;
    failWith('io-error', 'asset-sync failed');
  }
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
  // Real page-index read goes through adb dumpsys/logcat against the
  // emulator serial; throws device-unreachable when no device answers.
  // Matches the actual gallery presenter / nativeAdvancePage page markers
  // emitted on-device (gallery-page=N, GalleryPage index=N, or
  // NATIVE_ADVANCE_PAGE page=N), never an assumed format.
  const PAGE_PATTERNS = [
    /gallery-page[=:\s]+(\d+)/i,
    /gallerystorypage[=:\s]+(\d+)/i,
    /GalleryPage[^0-9]*(\d+)/,
    /NATIVE_ADVANCE_PAGE[^0-9]*page[=:\s]+(\d+)/i,
    /\bpage[=:\s]+(\d+)\s*\/\s*(\d+)/i,
    /\bpage-index[=:\s]+(\d+)/i,
  ];
  const scan = (text) => {
    if (!text) return null;
    for (const re of PAGE_PATTERNS) {
      const m = re.exec(text);
      if (m) return Number(m[1]);
    }
    return null;
  };
  try {
    const out = adbOrUnreachable(['shell', 'dumpsys', 'activity', 'top'], serial);
    const found = scan(out ? out.toString('utf8') : '');
    if (found !== null) return Promise.resolve(found);
  } catch (err) {
    if (err && err.code === 'device-unreachable') throw err;
    failWith('device-unreachable', `page index read failed: ${err.message}`);
  }
  try {
    const log = adbOrUnreachable(logcatFilterArgs(), serial);
    const found = scan(log ? log.toString('utf8') : '');
    if (found !== null) return Promise.resolve(found);
  } catch (err) {
    if (err && err.code === 'device-unreachable') throw err;
    failWith('device-unreachable', `page index logcat read failed: ${err.message}`);
  }
  // Reachable device with no recognizable marker: the wrap assertion cannot
  // be verified, so fail loudly rather than assuming index 0.
  failWith('device-unreachable', 'page index markers unavailable on device (no gallery-page/logcat marker)');
  return Promise.resolve(0);
}

// Valid logcat tag-level filter for the gallery markers. Logcat rejects a
// filter spec containing an embedded second colon, so the filter is the
// verified-good `GalleryPage:I` form (tag GalleryPage at Info + silence
// everything else).
function logcatFilterArgs() {
  return ['logcat', '-d', 'GalleryPage:I', '*:S'];
}

// APK / launch wiring for the proof deploy. The gallery APK is the signed
// x86_64 android-35 build; the signed adb-installable candidate
// (`build/m3-gallery-debug.apk`) is checked first. The CLI package output
// `dist/app-fat.apk` is NOT an installable APK (INSTALL_PARSE_FAILED_NOT_APK)
// and is never installed — it is not a candidate.
function resolveGalleryApkPath(opts) {
  if (opts && typeof opts.apkPath === 'string' && opts.apkPath.length > 0) return opts.apkPath;
  const galleryDir = (opts && opts.galleryDir)
    || path.resolve(__dirname, '..', '..', '..', 'examples', 'm3-gallery');
  const candidates = [
    path.join(galleryDir, 'build', 'm3-gallery-debug.apk'),
    path.join(galleryDir, 'build', 'm3-gallery-unsigned.apk'),
  ];
  for (const candidate of candidates) {
    try {
      if (fs.existsSync(candidate)) return candidate;
    } catch (_) { /* keep looking */ }
  }
  failWith('io-error', `gallery APK missing; no installable APK at ${candidates.join(' or ')}; run muse-native build first or pass --apk <path>`);
  return candidates[0];
}

const GALLERY_ACTIVITY = 'com.muse.host/.MainActivity';

// adb install of the gallery APK. Throws device-unreachable when no device
// answers; throws io-error when the APK file itself is missing/unreadable.
function defaultInstallApk({ serial, apkPath, galleryDir } = {}) {
  const apk = apkPath || resolveGalleryApkPath({ galleryDir });
  try {
    if (!fs.existsSync(apk)) {
      failWith('io-error', `gallery APK missing at ${apk}; run muse-native build first`);
    }
  } catch (err) {
    if (err && err.code === 'io-error') throw err;
    failWith('io-error', 'gallery APK check failed');
  }
  adbOrUnreachable(['install', '-r', apk], serial);
  return Promise.resolve({ installed: true, apk });
}

// Launch the gallery host activity.
function defaultLaunchApp({ serial } = {}) {
  adbOrUnreachable(['shell', 'am', 'start', '-n', GALLERY_ACTIVITY], serial);
  return Promise.resolve({ launched: true });
}

// Wait for gallery markers to appear on-device before any dumpsys/logcat
// count read. Polls dumpsys for the host pid plus the valid logcat filter;
// resolves once a marker is seen. `sleepMs`/`attempts`/`sleep` are
// injectable; the adb read itself stays on the real serial.
async function waitForGalleryMarkers({ serial, attempts, sleepMs, sleep } = {}) {
  const tries = Number.isInteger(attempts) && attempts > 0 ? attempts : 10;
  const delay = Number.isInteger(sleepMs) && sleepMs >= 0 ? sleepMs : 500;
  const sleepFn = sleep || ((ms) => new Promise((resolve) => { setTimeout(resolve, ms); }));
  const markerRe = /gallery-page|gallerystorypage|GalleryPage|NATIVE_ADVANCE_PAGE|com\.muse\.host/i;
  let lastErr = null;
  for (let i = 0; i < tries; i++) {
    try {
      const dumpsys = adbOrUnreachable(['shell', 'dumpsys', 'activity', 'top'], serial);
      if (dumpsys && markerRe.test(dumpsys.toString('utf8'))) return { ready: true };
      const log = adbOrUnreachable(logcatFilterArgs(), serial);
      if (log && markerRe.test(log.toString('utf8'))) return { ready: true };
    } catch (err) {
      lastErr = err;
      if (err && err.code === 'device-unreachable' && i === 0) {
        // Fall through and retry: the activity may still be starting.
      }
    }
    if (i + 1 < tries) await sleepFn(delay);
  }
  if (lastErr && lastErr.code === 'device-unreachable') throw lastErr;
  failWith('device-unreachable', 'gallery markers unavailable after launch (no GalleryPage/dumpsys marker)');
  return { ready: false };
}

async function runGalleryProof({ serial, androidJar, outDir, readCounts, capture, advance, readIndex, sync, galleryDir, apkPath, install, launch, waitForMarkers, sleep }) {
  const deviceSerial = serial || process.env.ANDROID_SERIAL;
  if (!deviceSerial) {
    failWith('device-unreachable', 'no serial/device');
  }
  const jar = androidJar || process.env.ANDROID_JAR;
  if (!jar) {
    failWith('device-unreachable', 'no android.jar/device toolchain');
  }
  const resolvedOut = outDir || path.resolve(DEFAULT_OUT_SUFFIX);

  // T4 hygiene: install/asset-sync first — copy the current
  // dist/tree.mui.bin into assets/ so the rebuild + proof installs a fresh
  // tree and the APK never goes stale vs the tree.
  const syncFn = sync || ((dir) => syncAssets(dir));
  try {
    syncFn(galleryDir);
  } catch (err) {
    if (err && (err.code === 'io-error' || /missing build output/.test(err.message || ''))) throw err;
    failWith('device-unreachable', `asset-sync failed: ${(err && err.message) || String(err)}`);
  }

  // Device deploy (production path only): adb-install the x86_64 android-35
  // gallery APK, launch com.muse.host/.MainActivity, then wait for gallery
  // markers before any dumpsys/logcat read. The install runs after the
  // asset sync so the fresh tree reaches the installed APK. Hermetic halves
  // inject readCounts/capture and never touch a device, so the deploy is
  // skipped whenever device I/O is injected; every deploy helper is
  // injectable (`install`/`launch`/`waitForMarkers`) for the same reason.
  // Deploy failures map to device-unreachable; io-error (missing APK) is
  // preserved as-is.
  const hermetic = Boolean(readCounts || capture);
  if (!hermetic) {
    const installFn = install || ((args) => defaultInstallApk(args));
    const launchFn = launch || ((args) => defaultLaunchApp(args));
    const waitFn = waitForMarkers || ((args) => waitForGalleryMarkers(args));
    try {
      await installFn({ serial: deviceSerial, apkPath, galleryDir });
    } catch (err) {
      if (err && err.code === 'io-error') throw err;
      failWith('device-unreachable', `apk install failed: ${(err && err.message) || String(err)}`);
    }
    try {
      await launchFn({ serial: deviceSerial });
    } catch (err) {
      failWith('device-unreachable', `activity launch failed: ${(err && err.message) || String(err)}`);
    }
    try {
      await waitFn({ serial: deviceSerial, sleep });
    } catch (err) {
      if (err && err.code === 'device-unreachable') throw err;
      failWith('device-unreachable', `gallery markers wait failed: ${(err && err.message) || String(err)}`);
    }
  }

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
  const advanceForShots = advance || ((opts) => defaultAdvancePage({ ...opts, serial: deviceSerial }));
  try {
    fs.mkdirSync(resolvedOut, { recursive: true });
  } catch (err) {
    failWith('io-error', 'screenshot write failed');
  }
  const shots = [];
  for (let page = 0; page < pages; page++) {
    const frame = await captureViewportFn({ serial: deviceSerial, page });
    if (!frame || isBlankViewport(frame)) {
      failWith('blank-viewport', `page ${page} viewport is blank`);
    }
    const shot = path.join(resolvedOut, `page-${String(page + 1).padStart(2, '0')}.png`);
    await saveScreenshot({ serial: deviceSerial, page, shot, frame });
    shots.push(shot);
    if (page + 1 < pages) {
      await advanceForShots({ nativeAdvancePage: true, serial: deviceSerial, page });
    }
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
  // Resolved live on the emulator via adb against ANDROID_SERIAL
  // (default 127.0.0.1:6562); hermetic halves inject their own reader or
  // assert deriveExpectedCounts directly. Parses the real gallery presenter
  // counts (rows and pages) from dumpsys output, falling back to logcat page
  // markers; a genuinely unreachable device throws device-unreachable, and a
  // reachable device with no parseable counts throws counts-mismatch —
  // never a silent null pass.
  const scanPairs = (text) => {
    if (!text) return null;
    let m = text.match(/gallery-rows\s*[:=]\s*(\d+)[\s\S]*?gallery-pages\s*[:=]\s*(\d+)/i);
    if (m) return { rows: Number(m[1]), pages: Number(m[2]) };
    m = text.match(/gallery-pages\s*[:=]\s*(\d+)[\s\S]*?gallery-rows\s*[:=]\s*(\d+)/i);
    if (m) return { rows: Number(m[2]), pages: Number(m[1]) };
    m = text.match(/rows\s*[:=]\s*(\d+)\s+pages\s*[:=]\s*(\d+)/i);
    if (m) return { rows: Number(m[1]), pages: Number(m[2]) };
    return null;
  };
  let dumpsysText = '';
  try {
    const out = adbOrUnreachable(['shell', 'dumpsys', 'activity', 'top'], serial);
    dumpsysText = out ? out.toString('utf8') : '';
  } catch (err) {
    if (err && err.code === 'device-unreachable') throw err;
    failWith('device-unreachable', 'device counts read failed: ' + err.message);
  }
  const direct = scanPairs(dumpsysText);
  if (direct) return Promise.resolve(direct);
  try {
    const log = adbOrUnreachable(logcatFilterArgs(), serial);
    const text = dumpsysText + '\n' + (log ? log.toString('utf8') : '');
    const paired = scanPairs(text);
    if (paired) return Promise.resolve(paired);
    let maxPage = -1;
    let total = -1;
    const pageRe = /gallery-page\s*[:=]\s*(\d+)\s*\/\s*(\d+)/gi;
    let pm = null;
    while ((pm = pageRe.exec(text)) !== null) {
      if (Number(pm[1]) > maxPage) maxPage = Number(pm[1]);
      total = Number(pm[2]);
    }
    if (maxPage >= 0 && total > 0) {
      return Promise.resolve({ rows: total * ROWS_PER_PAGE, pages: total });
    }
  } catch (err) {
    if (err && (err.code === 'device-unreachable' || err.code === 'counts-mismatch')) throw err;
    failWith('device-unreachable', 'device counts logcat read failed: ' + err.message);
  }
  failWith('counts-mismatch', 'device counts unavailable: no parseable rows or pages on device output');
  return Promise.resolve(null);
}

function decodePngToPixels(png) {
  // Minimal PNG (color-type 2/6, bit-depth 8) decoder over node:zlib: parses
  // IHDR plus IDAT plus IEND, inflates the scanlines, unfilters, and returns
  // raw RGBA bytes for isBlankViewport. Returns null when the bytes are not
  // a decodable PNG (caller then treats the framebuffer read as failed).
  try {
    if (!png || png.length < 8) return null;
    if (png[0] !== 0x89 || png[1] !== 0x50 || png[2] !== 0x4e || png[3] !== 0x47) return null;
    let pos = 8;
    let width = 0;
    let height = 0;
    let colorType = -1;
    const idat = [];
    while (pos + 8 <= png.length) {
      const len = png.readUInt32BE(pos);
      const type = png.toString('ascii', pos + 4, pos + 8);
      const data = png.subarray(pos + 8, pos + 8 + len);
      if (type === 'IHDR') {
        width = data.readUInt32BE(0);
        height = data.readUInt32BE(4);
        colorType = data[9];
      } else if (type === 'IDAT') {
        idat.push(data);
      } else if (type === 'IEND') {
        break;
      }
      pos += 12 + len;
    }
    if (!width || !height || !idat.length) return null;
    if (colorType !== 2 && colorType !== 6) return null;
    const channels = colorType === 6 ? 4 : 3;
    const raw = require('node:zlib').inflateSync(Buffer.concat(idat));
    const stride = width * channels;
    const pixels = Buffer.alloc(width * height * 4);
    let rp = 0;
    let wp = 0;
    let prev = Buffer.alloc(stride, 0);
    for (let y = 0; y < height; y++) {
      const filter = raw[rp++];
      const cur = raw.subarray(rp, rp + stride);
      rp += stride;
      const line = Buffer.alloc(stride);
      for (let x = 0; x < stride; x++) {
        const a = x >= channels ? line[x - channels] : 0;
        const b = prev[x];
        const c = x >= channels ? prev[x - channels] : 0;
        let v = cur[x];
        if (filter === 1) v = (v + a) & 0xff;
        else if (filter === 2) v = (v + b) & 0xff;
        else if (filter === 3) v = (v + (((a + b) / 2) | 0)) & 0xff;
        else if (filter === 4) {
          const pa = Math.abs(b - c);
          const pb = Math.abs(a - c);
          const pc = Math.abs(a + b - 2 * c);
          const pr = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
          v = (v + pr) & 0xff;
        }
        line[x] = v;
      }
      for (let x = 0; x < width; x++) {
        pixels[wp++] = line[x * channels];
        pixels[wp++] = line[x * channels + 1];
        pixels[wp++] = line[x * channels + 2];
        pixels[wp++] = channels === 4 ? line[x * channels + 3] : 0xff;
      }
      prev = line;
    }
    return { width, height, pixels };
  } catch (_) {
    return null;
  }
}

function captureViewport({ serial, page } = {}) {
  // Screencap read over adb; throws device-unreachable when no device
  // answers. Decodes the PNG screenshot into rendered RGBA pixels for
  // isBlankViewport (blank means uniform rendered pixels, i.e. zero
  // variance or diagnostic-only output) — never runs the blank detector
  // over raw PNG bytes, whose header and compression variance is vacuous
  // on-device. A null return means "no frame" and the caller rejects
  // blank-viewport.
  try {
    const out = adbOrUnreachable(['exec-out', 'screencap', '-p'], serial);
    if (!out || out.length === 0) return Promise.resolve(null);
    const png = Buffer.from(out);
    const decoded = decodePngToPixels(png);
    if (!decoded) return Promise.resolve(null);
    return Promise.resolve({ width: decoded.width, height: decoded.height, pixels: decoded.pixels, png, page });
  } catch (err) {
    if (err && err.code === 'device-unreachable') throw err;
    failWith('device-unreachable', 'screencap read failed: ' + err.message);
  }
  return Promise.resolve(null);
}

function saveScreenshot({ serial, page, shot, frame } = {}) {
  // Persists the captured frame bytes for this page to outDir page-NN.png
  // so shots[] are real artifacts. Prefers the in-hand frame.png bytes (a
  // valid PNG); falls back to a fresh screencap when no PNG bytes were
  // handed in. Never writes raw RGBA pixels to the .png path and never
  // writes a 0-byte placeholder.
  const writeBytes = (target, bytes) => {
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.writeFileSync(target, bytes);
  };
  try {
    if (frame && Buffer.isBuffer(frame.png) && frame.png.length > 0) {
      writeBytes(shot, frame.png);
      return Promise.resolve(shot);
    }
    const out = adbOrUnreachable(['exec-out', 'screencap', '-p'], serial);
    if (!out || out.length === 0) {
      failWith('blank-viewport', 'page ' + page + ' screenshot unavailable');
    }
    writeBytes(shot, Buffer.from(out));
  } catch (err) {
    if (err && (err.code === 'blank-viewport' || err.code === 'device-unreachable')) throw err;
    failWith('io-error', 'screenshot write failed');
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
  logcatFilterArgs,
  resolveGalleryApkPath,
  defaultInstallApk,
  defaultLaunchApp,
  waitForGalleryMarkers,
};
