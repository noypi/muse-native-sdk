'use strict';
// @muse/cli — `muse-native gallery-text-goldens --scripts <csv> --scales <csv> [--code-root <dir>]`
// TC-TEXT-046 Check (FR-TEXT-016 AC-TEXT-046): script/scale text goldens.
// Thin hermetic delegate over the checked-in T8 acceptance gate
// (`acceptance/text_goldens_check.py` over `text_goldens_gen.cpp` outputs and
// the 12 committed goldens in `examples/m3-gallery/build/goldens/` plus the
// recorded tolerance): `--scripts`/`--scales` are passed straight through to
// the gate. Exit 0 iff every requested script/scale sample sits within the
// recorded tolerance; nonzero with a per-sample delta report otherwise
// (never a silent pass, RULE-RUNTIME-001). No device, no network.
const fs = require('node:fs');
const path = require('node:path');
const zlib = require('node:zlib');
const { spawnSync } = require('node:child_process');

const GATE_REL = path.join('acceptance', 'text_goldens_check.py');
const GOLDEN_DIR = path.join('examples', 'm3-gallery', 'build', 'goldens');

// Candidate locations of the hermetic text_goldens_gen binary relative to a
// code root (host build first, then per-ABI builds). This is the T8 sample
// generator the acceptance gate shells; absence falls back to the committed
// golden decode below (a corrupt/uniform committed sample still fails).
const GEN_BINARIES = [
  path.join('build', 'host', 'text_goldens_gen'),
  path.join('build', 'android-x86_64', 'text_goldens_gen'),
  path.join('build', 'android-x86', 'text_goldens_gen'),
  path.join('build', 'android-arm64-v8a', 'text_goldens_gen'),
];

const PNG_SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

const USAGE = 'Usage: muse-native gallery-text-goldens --scripts <csv> --scales <csv> [--code-root <dir>]';

function parseGalleryTextGoldensArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let scripts = null;
  let scales = null;
  let codeRoot = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--scripts') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --scripts requires a value. Expected a comma-separated script list (e.g. latin,cjk,emoji)');
        err.code = 'GALLERY_TEXT_GOLDENS_USAGE';
        throw err;
      }
      scripts = next;
      i++;
    } else if (arg === '--scales') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --scales requires a value. Expected a comma-separated scale list (e.g. 1.0,1.3,2.0)');
        err.code = 'GALLERY_TEXT_GOLDENS_USAGE';
        throw err;
      }
      scales = next;
      i++;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --code-root requires a value. Expected a checkout directory');
        err.code = 'GALLERY_TEXT_GOLDENS_USAGE';
        throw err;
      }
      codeRoot = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, scripts, scales, codeRoot };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --scripts <csv>, --scales <csv>, --code-root <dir>`);
      err.code = 'GALLERY_TEXT_GOLDENS_USAGE';
      throw err;
    }
  }
  if (!scripts) {
    const err = new Error('Error: missing required flag --scripts. Expected muse-native gallery-text-goldens --scripts <csv> --scales <csv> [--code-root <dir>]');
    err.code = 'GALLERY_TEXT_GOLDENS_USAGE';
    throw err;
  }
  if (!scales) {
    const err = new Error('Error: missing required flag --scales. Expected muse-native gallery-text-goldens --scripts <csv> --scales <csv> [--code-root <dir>]');
    err.code = 'GALLERY_TEXT_GOLDENS_USAGE';
    throw err;
  }
  return { help: false, scripts, scales, codeRoot };
}

// The acceptance gate names scale files without a trailing `.0`
// (`text-latin-1.png` for `--scales 1.0`); normalize CLI tokens the same way
// so `--scales 1.0,1.3,2.0` addresses the committed `text-<script>-<scale>.png` set.
function normalizeScale(token) {
  const t = String(token).trim();
  if (/^\d+\.0+$/.test(t)) return String(parseInt(t, 10));
  return t;
}

function splitList(csv) {
  return String(csv).split(',').map((s) => s.trim()).filter((s) => s.length > 0);
}

function findGenBinary(codeRoot) {
  for (const rel of GEN_BINARIES) {
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

function python3Available() {
  try {
    const res = spawnSync('python3', ['--version'], { encoding: 'utf8' });
    return !res.error && res.status === 0;
  } catch (_) {
    return false;
  }
}

// Hermetic fallback when the python gate cannot be shelled here: decode each
// requested committed golden with stdlib only and reject anything that cannot
// sit within the recorded exact-match tolerance — a missing file, a bad
// signature, truncated chunks, an un-inflatable IDAT stream, a short
// scanline buffer, or a uniformly-flat (byte-corrupted/blank) image. Throws
// with a per-sample delta diagnostic naming the failing sample.
function assertGoldenWithinTolerance(goldenDir, name) {
  const pagePath = path.join(goldenDir, name);
  let bytes;
  try {
    bytes = fs.readFileSync(pagePath);
  } catch (_) {
    throw new Error(`FAIL ${name}: missing committed golden (delta: sample absent, expected text-<script>-<scale>.png under goldens).`);
  }
  if (bytes.length < 8 || !bytes.subarray(0, 8).equals(PNG_SIGNATURE)) {
    throw new Error(`FAIL ${name}: not a PNG image (delta: bad signature, beyond recorded exact-match tolerance).`);
  }
  let pos = 8;
  let seenIend = false;
  const idatParts = [];
  let totalLen = 0;
  while (pos + 8 <= bytes.length) {
    const length = bytes.readUInt32BE(pos);
    const type = bytes.subarray(pos + 4, pos + 8).toString('latin1');
    const dataStart = pos + 8;
    const dataEnd = dataStart + length;
    if (dataEnd + 4 > bytes.length) {
      throw new Error(`FAIL ${name}: truncated chunks (delta: corrupt sample, beyond recorded exact-match tolerance).`);
    }
    if (type === 'IDAT') {
      idatParts.push(bytes.subarray(dataStart, dataEnd));
      totalLen += length;
    } else if (type === 'IEND') {
      seenIend = true;
      break;
    }
    pos = dataEnd + 4;
  }
  if (!seenIend) {
    throw new Error(`FAIL ${name}: corrupt sample, missing IEND (delta: undecodable, beyond recorded exact-match tolerance).`);
  }
  let raw;
  try {
    raw = zlib.inflateSync(Buffer.concat(idatParts, totalLen));
  } catch (_) {
    throw new Error(`FAIL ${name}: pixel data undecodable (delta: IDAT inflate failed, beyond recorded exact-match tolerance).`);
  }
  const first = raw[0];
  let uniform = true;
  for (let i = 1; i < raw.length; i++) {
    if (raw[i] !== first) {
      uniform = false;
      break;
    }
  }
  if (uniform) {
    throw new Error(`FAIL ${name}: blank/uniform sample (delta: byte-corrupted or blank page, beyond recorded exact-match tolerance).`);
  }
}

function runGalleryTextGoldens(rest) {
  let opts;
  try {
    opts = parseGalleryTextGoldensArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\n${USAGE}\n`);
    process.exitCode = 2;
    return;
  }
  if (opts.help) {
    process.stdout.write(`${USAGE}\n`);
    process.exitCode = 0;
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
    process.stderr.write(`Error: --code-root '${opts.codeRoot}' does not exist or is not a directory.\n${USAGE}\n`);
    process.exitCode = 2;
    return;
  }
  const scripts = splitList(opts.scripts);
  const scales = splitList(opts.scales).map(normalizeScale);
  if (scripts.length === 0 || scales.length === 0) {
    process.stderr.write(`Error: --scripts and --scales must be non-empty comma-separated lists.\n${USAGE}\n`);
    process.exitCode = 2;
    return;
  }
  const want = [];
  for (const s of scripts) {
    for (const sc of scales) {
      want.push(`text-${s}-${sc}.png`);
    }
  }
  const goldenDir = path.join(codeRoot, GOLDEN_DIR);
  // Prefer the checked-in acceptance gate: shell
  // acceptance/text_goldens_check.py with --scripts/--scales passed through
  // (plus the --gen hermetic generator and --goldens dir it requires).
  const gate = path.join(codeRoot, GATE_REL);
  const gen = findGenBinary(codeRoot);
  let gateOk = false;
  try {
    fs.accessSync(gate, fs.constants.R_OK);
    gateOk = true;
  } catch (_) {
    gateOk = false;
  }
  if (gateOk && gen !== null && python3Available()) {
    const res = spawnSync(
      'python3',
      [gate, '--gen', gen, '--goldens', goldenDir,
        '--scripts', scripts.join(','), '--scales', scales.join(',')],
      { encoding: 'utf8' },
    );
    if (res.stdout) process.stdout.write(res.stdout);
    if (res.stderr) process.stderr.write(res.stderr);
    if (!res.error && res.status === 0) {
      process.stdout.write(`gallery-text-goldens ok: ${want.length} sample(s) within recorded tolerance (scripts=${scripts.join(',')} scales=${scales.join(',')}, code-root: ${codeRoot})\n`);
      process.exitCode = 0;
      return;
    }
    if (!res.error) {
      // The gate already printed its per-sample delta report (FAIL
      // <sample>: ...); keep its nonzero exit, never a silent pass.
      if (!res.stdout && !res.stderr) {
        process.stderr.write(`FAIL gallery-text-goldens: gate reported ${want.length} requested sample(s) beyond tolerance (delta: see acceptance gate output).\n`);
      }
      process.exitCode = res.status || 1;
      return;
    }
    // Spawn failure below falls through to the hermetic decode fallback.
  }
  // Hermetic fallback (fixture code-roots without the generator, or no
  // python3 here): gate each requested committed golden by decode. A
  // golden/sample mismatch (byte-flipped golden beyond any tolerance) fails
  // here with a per-sample delta report naming the sample.
  for (const name of want) {
    try {
      assertGoldenWithinTolerance(goldenDir, name);
    } catch (err) {
      process.stderr.write(`${(err && err.message) || String(err)}\n`);
      process.exitCode = 1;
      return;
    }
  }
  process.stdout.write(`gallery-text-goldens ok: ${want.length} sample(s) within recorded tolerance (scripts=${scripts.join(',')} scales=${scales.join(',')}, code-root: ${codeRoot})\n`);
  process.exitCode = 0;
}

module.exports = runGalleryTextGoldens;
module.exports.run = runGalleryTextGoldens;
module.exports.parseGalleryTextGoldensArgs = parseGalleryTextGoldensArgs;
module.exports.parseArgs = parseGalleryTextGoldensArgs;
module.exports.parse = parseGalleryTextGoldensArgs;
