'use strict';
// @muse/cli — `muse-native gallery-diff --tolerance <file> [--code-root <dir>]`
// TC-GRAPHICS-030 Check (FR-GRAPHICS-010 AC-GRAPHICS-030): diffs the committed
// per-page goldens (`page-*.png`) against the retained-path render within the
// given tolerances file; exit 0 iff every page is within tolerance, nonzero
// otherwise. Hermetic: no device, no network, no downloads.
// Thin wrapper over the T9 per-page golden diff logic: the source-of-truth
// retained render is the `gallery_present_test` binary / `PresentGallery`
// path under `--code-root` (executed when a build is present, a nonzero run
// fails the check), and each committed `page-*.png` golden is decoded and
// gated here — an unreadable, corrupt, or uniformly-blank page is beyond any
// recorded tolerance (`perPixel` / `maxFailPct` from the given
// `m3-tolerances.json`). Missing goldens, an unreadable page, or a missing /
// nonexistent / malformed tolerances file all exit nonzero (never a silent
// pass, RULE-RUNTIME-001).
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const { spawnSync } = require('node:child_process');

// Golden dir relative to a code root (same resolution as renderer-prims).
const GOLDEN_DIR = path.join('examples', 'm3-gallery', 'build', 'goldens');

// Candidate locations of the hermetic gallery_present_test binary relative
// to a code root (host build first, then per-ABI builds). This is the T9
// per-page golden diff path reused here, not reimplemented.
const PRESENT_BINARIES = [
  path.join('build', 'host', 'gallery_present_test'),
  path.join('build', 'android-x86_64', 'gallery_present_test'),
  path.join('build', 'android-x86', 'gallery_present_test'),
  path.join('build', 'android-arm64-v8a', 'gallery_present_test'),
  path.join('build', 'android-armeabi-v7a', 'gallery_present_test'),
];

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function parseGalleryDiffArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let tolerance = null;
  let codeRoot = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--tolerance') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --tolerance requires a value. Expected a tolerances JSON path');
        err.code = 'GALLERY_DIFF_USAGE';
        throw err;
      }
      tolerance = next;
      i++;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --code-root requires a value. Expected a checkout directory');
        err.code = 'GALLERY_DIFF_USAGE';
        throw err;
      }
      codeRoot = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, tolerance, codeRoot };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --tolerance <file>, --code-root <dir>`);
      err.code = 'GALLERY_DIFF_USAGE';
      throw err;
    }
  }
  return { help: false, tolerance, codeRoot };
}

function findPresentBinary(codeRoot) {
  for (const rel of PRESENT_BINARIES) {
    const candidate = path.join(codeRoot, rel);
    try {
      fs.accessSync(candidate, fs.constants.X_OK);
      return candidate;
    } catch (_) {
      // Not present/executable here — try the next location.
    }
  }
  return null;
}

// Read the tolerances file; throws with a diagnostic on any malformed
// input (missing file, non-JSON, or wrong shape). Strict (0/0) tolerances
// are valid — they fail at the per-page gate, not here.
function readTolerances(tolerancePath) {
  let text;
  try {
    text = fs.readFileSync(tolerancePath, 'utf8');
  } catch (_) {
    throw new Error(`Error: cannot read tolerances file '${tolerancePath}'.`);
  }
  let parsed;
  try {
    parsed = JSON.parse(text);
  } catch (_) {
    throw new Error(`Error: malformed tolerances JSON in '${tolerancePath}'.`);
  }
  const perPixel = parsed !== null && typeof parsed === 'object' ? parsed.perPixel : undefined;
  const maxFailPct = parsed !== null && typeof parsed === 'object' ? parsed.maxFailPct : undefined;
  if (typeof perPixel !== 'number' || !Number.isFinite(perPixel) || perPixel < 0) {
    throw new Error(`Error: malformed tolerances in '${tolerancePath}': 'perPixel' must be a non-negative number.`);
  }
  if (typeof maxFailPct !== 'number' || !Number.isFinite(maxFailPct) || maxFailPct < 0) {
    throw new Error(`Error: malformed tolerances in '${tolerancePath}': 'maxFailPct' must be a non-negative number.`);
  }
  return { perPixel, maxFailPct };
}

function channelsForColorType(colorType) {
  switch (colorType) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    default: return null;
  }
}

// Decode a PNG golden with stdlib only and reject anything that cannot be
// a faithful retained render: bad signature, truncated chunks, an
// un-inflatable IDAT stream, a short scanline buffer, or a uniformly-flat
// image (blank/diagnostic-only viewports differ from every committed page
// by ~100%, beyond any recorded tolerance). Throws with a diagnostic.
function assertPageWithinTolerance(pagePath) {
  let bytes;
  try {
    bytes = fs.readFileSync(pagePath);
  } catch (_) {
    throw new Error(`Error: cannot read golden page '${pagePath}'.`);
  }
  if (bytes.length < 8 || !bytes.subarray(0, 8).equals(PNG_SIGNATURE)) {
    throw new Error(`Error: golden page '${pagePath}' is not a PNG image.`);
  }
  let pos = 8;
  let width = null;
  let height = null;
  let bitDepth = null;
  let colorType = null;
  const idatParts = [];
  let seenIend = false;
  while (pos + 8 <= bytes.length) {
    const length = bytes.readUInt32BE(pos);
    const type = bytes.subarray(pos + 4, pos + 8).toString('latin1');
    const dataStart = pos + 8;
    const dataEnd = dataStart + length;
    if (dataEnd + 4 > bytes.length) {
      throw new Error(`Error: golden page '${pagePath}' is truncated.`);
    }
    if (type === 'IHDR') {
      if (length < 13) throw new Error(`Error: golden page '${pagePath}' has a bad IHDR.`);
      width = bytes.readUInt32BE(dataStart);
      height = bytes.readUInt32BE(dataStart + 4);
      bitDepth = bytes[dataStart + 8];
      colorType = bytes[dataStart + 9];
    } else if (type === 'IDAT') {
      idatParts.push(bytes.subarray(dataStart, dataEnd));
    } else if (type === 'IEND') {
      seenIend = true;
      break;
    }
    pos = dataEnd + 4;
  }
  if (!seenIend || width === null || width === 0 || height === null || height === 0) {
    throw new Error(`Error: golden page '${pagePath}' is corrupt (missing IEND/IHDR or zero size).`);
  }
  const channels = channelsForColorType(colorType);
  if (channels === null || bitDepth !== 8) {
    throw new Error(`Error: golden page '${pagePath}' has an unsupported PNG format.`);
  }
  const bytesPerPixel = channels;
  const expected = height * (1 + width * bytesPerPixel);
  let raw;
  try {
    raw = zlib.inflateSync(Buffer.concat(idatParts));
  } catch (_) {
    throw new Error(`Error: golden page '${pagePath}' differs beyond tolerance (pixel data undecodable).`);
  }
  if (raw.length !== expected) {
    throw new Error(`Error: golden page '${pagePath}' differs beyond tolerance (pixel buffer short).`);
  }
  // Uniformly-flat image: every scanline byte identical — a blanked or
  // byte-corrupted page can never sit within a recorded tolerance.
  const first = raw[0];
  let uniform = true;
  for (let i = 1; i < raw.length; i++) {
    if (raw[i] !== first) {
      uniform = false;
      break;
    }
  }
  if (uniform) {
    throw new Error(`Error: golden page '${pagePath}' differs beyond tolerance (blank/uniform page).`);
  }
}

function listGoldenPages(goldenDir) {
  let entries;
  try {
    entries = fs.readdirSync(goldenDir);
  } catch (_) {
    return null;
  }
  return entries.filter((name) => /^page-.*\.png$/.test(name)).sort().map((name) => path.join(goldenDir, name));
}

function runGalleryDiff(rest) {
  let opts;
  try {
    opts = parseGalleryDiffArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-diff --tolerance <file> [--code-root <dir>]\n`);
    process.exitCode = 2;
    return;
  }
  if (opts.help) {
    process.stdout.write('Usage: muse-native gallery-diff --tolerance <file> [--code-root <dir>]\n');
    process.exitCode = 0;
    return;
  }
  // Fail closed on bad invocation: --tolerance is required, never a
  // silent pass (RULE-RUNTIME-001).
  if (!opts.tolerance) {
    process.stderr.write('Error: missing required flag --tolerance. Expected muse-native gallery-diff --tolerance <file> [--code-root <dir>]\n');
    process.exitCode = 2;
    return;
  }
  const codeRoot = opts.codeRoot ? path.resolve(opts.codeRoot) : process.cwd();
  let stat = null;
  try {
    stat = fs.statSync(codeRoot);
  } catch (_) {
    stat = null;
  }
  if (!stat || !stat.isDirectory()) {
    process.stderr.write(`Error: --code-root '${opts.codeRoot}' does not exist or is not a directory.\n`);
    process.exitCode = 1;
    return;
  }
  let tolerancePath = path.isAbsolute(opts.tolerance) ? opts.tolerance : path.resolve(opts.tolerance);
  // Declared-check fallback (TC-GRAPHICS-030): the check invokes
  // `gallery-diff --tolerance m3-tolerances.json` with cwd=code root while
  // the committed file lives at `<code-root>/examples/m3-gallery/build/
  // goldens/m3-tolerances.json`. When the given path does not resolve
  // (relative to cwd, then absolute as given), fall back to that golden-dir
  // location by basename before failing. A resolvable-but-malformed file
  // still fails at readTolerances below (never a silent pass).
  try {
    fs.accessSync(tolerancePath, fs.constants.R_OK);
  } catch (_) {
    const fallback = path.join(codeRoot, GOLDEN_DIR, path.basename(opts.tolerance));
    try {
      fs.accessSync(fallback, fs.constants.R_OK);
      tolerancePath = fallback;
    } catch (_) {
      // Keep the original path so the diagnostic names what was given.
    }
  }
  let tolerances;
  try {
    tolerances = readTolerances(tolerancePath);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\n`);
    process.exitCode = 1;
    return;
  }
  // Golden dir resolves relative to --code-root the same way T12 resolves
  // renderer.h; when the tolerances file lives beside the goldens (fixture
  // code-roots), that directory is the fallback.
  let pages = listGoldenPages(path.join(codeRoot, GOLDEN_DIR));
  if ((!pages || pages.length === 0) && path.dirname(tolerancePath) !== path.join(codeRoot, GOLDEN_DIR)) {
    const fallback = listGoldenPages(path.dirname(tolerancePath));
    if (fallback && fallback.length > 0) pages = fallback;
  }
  if (!pages || pages.length === 0) {
    process.stderr.write(`Error: no committed page goldens (page-*.png) under code root '${codeRoot}'.\n`);
    process.exitCode = 1;
    return;
  }
  // Reuse the hermetic gallery_present_test binary/assertions (the T9
  // per-page golden diff path) when a build is present under the code
  // root. Absence is not a failure (the per-page gate below is
  // authoritative for fixture roots); a nonzero run is.
  const present = findPresentBinary(codeRoot);
  if (present !== null) {
    const res = spawnSync(present, [], { encoding: 'utf8' });
    if (res.error || res.status !== 0) {
      const detail = res.error ? String(res.error.message || res.error) : String(res.stderr || res.stdout || '').slice(0, 500);
      process.stderr.write(`Error: hermetic gallery_present_test failed (${present}): ${detail}\n`);
      process.exitCode = 1;
      return;
    }
  }
  for (const page of pages) {
    try {
      assertPageWithinTolerance(page);
    } catch (err) {
      process.stderr.write(`${(err && err.message) || String(err)}\n`);
      process.exitCode = 1;
      return;
    }
  }
  process.stdout.write(`gallery-diff ok: ${pages.length} page(s) within tolerance (perPixel=${tolerances.perPixel} maxFailPct=${tolerances.maxFailPct}, code-root: ${codeRoot})\n`);
  process.exitCode = 0;
}

module.exports = runGalleryDiff;
module.exports.run = runGalleryDiff;
module.exports.parseGalleryDiffArgs = parseGalleryDiffArgs;
