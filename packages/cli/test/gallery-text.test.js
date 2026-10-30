'use strict';
// T14 — `muse-native gallery-text` + `muse-native gallery-text-goldens` thin delegates
// (TC-TEXT-045/046 Checks). Spec refs: FR-TEXT-016, AC-TEXT-045/046.
// Interfaces (plan.md verbatim):
//   `muse-native gallery-text --assert-vector --assert-atlas-reuse [--code-root <dir>] -> exit code`
//   — exit 0 = vector path asserted (nonzero `ShapedText` coverage, zero bitmap
//   `DrawText` callers on the gallery path) and atlas reuse asserted (cache hits
//   across frames per font/scale, bounded entries/bytes); nonzero + per-sample
//   delta report on failure, never a silent pass.
//   `muse-native gallery-text-goldens --scripts latin,cjk,emoji --scales 1.0,1.3,2.0 [--code-root <dir>] -> exit code`
//   — exit 0 = every script/scale sample within the committed tolerance;
//   nonzero + diff artifact + per-sample delta report on failure, never a silent
//   pass. Unknown/missing flags exit 2 with usage.
// Detail under test: mirror the `gallery-diff` registration pattern exactly:
// extend `COMMANDS` + usage/help strings + dispatch branches in
// `packages/cli/lib/cli.js` for both subcommands (same `exit 2` usage-error
// shape as `gallery-diff`/`gallery-scroll`), with one thin per-command module
// each (`gallery-text.js`, `gallery-text-goldens.js`) exporting a callable
// entry point plus an arg parser, following `packages/cli/lib/gallery-diff.js`
// / `gallery-scroll.js`. Reuse, don't reimplement, the gate: `gallery-text`
// invokes the same pixel-exact comparison + tolerance + atlas-reuse assertions
// the tester proved (bitmap-counter-0 + atlas hit/bound checks over the
// T5/T6/T3 paths); `gallery-text-goldens` shells the checked-in
// `acceptance/text_goldens_check.py` (over `text_goldens_gen.cpp` outputs and
// the 12 committed goldens in `examples/m3-gallery/build/goldens/` + tolerance
// record) with `--scripts`/`--scales` passed through.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path/child_process)
// only. Spawns `node bin/muse-native.js` against tmpdir fixtures (commands
// .test.js pattern) for the CLI exit contract; the python gate itself is
// NEVER spawned here — arg pass-through is asserted by parser unit tests +
// module-source reference to the checked-in gate path, no device, no network,
// no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1: like `gallery-diff`/`gallery-scroll`, both modules accept an optional
//   `--code-root <dir>` resolving the tree under audit (default: cwd / run
//   checkout). Coder owns detection internals; reconcile paths, not verdicts.
// - A-T2: the `gallery-text-goldens` negative (golden/sample mismatch) is
//   simulated with a fixture code-root whose golden bytes are corrupted beyond
//   any tolerance (byte-flipped copy) paired with strict tolerances —
//   fixture-based, no device, no production-code touch. The wrapper must exit
//   nonzero with a per-sample delta report naming the failing sample, never a
//   silent pass.
// - A-T3: the python gate (`acceptance/text_goldens_check.py`) is stubbed at
//   unit scope: no `python3` is spawned by this file. Pass-through of
//   `--scripts`/`--scales` is asserted via the arg parser round-trip plus a
//   source reference to the gate path; the coder wires the real
//   `child_process.spawnSync(python, [gate, --scripts, --scales, ...])` call.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const CODE_ROOT = process.env.MUSE_CODE_ROOT || REPO_ROOT;

let galleryText = null;
try {
  // eslint-disable-next-line global-require
  galleryText = require('../lib/gallery-text.js');
} catch {
  galleryText = null;
}

let galleryTextGoldens = null;
try {
  // eslint-disable-next-line global-require
  galleryTextGoldens = require('../lib/gallery-text-goldens.js');
} catch {
  galleryTextGoldens = null;
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function callableEntry(mod) {
  if (typeof mod === 'function') return mod;
  if (mod && typeof mod.run === 'function') return mod.run;
  return null;
}

function argParser(mod, names) {
  for (const n of names) {
    if (mod && typeof mod[n] === 'function') return mod[n];
  }
  return null;
}

describe('T14 — gallery-text + gallery-text-goldens module surface (AC-TEXT-045/046)', () => {
  it('packages/cli/lib/gallery-text.js exists and loads', () => {
    assert.ok(galleryText !== null, 'lib/gallery-text.js must exist (thin delegate module)');
  });

  it('packages/cli/lib/gallery-text-goldens.js exists and loads', () => {
    assert.ok(galleryTextGoldens !== null, 'lib/gallery-text-goldens.js must exist (thin delegate module)');
  });

  it('gallery-text module exports a callable entry point plus an arg parser', () => {
    assert.ok(galleryText, 'lib/gallery-text.js must load');
    assert.equal(typeof callableEntry(galleryText), 'function', 'gallery-text module must export a callable entry point');
    const parse = argParser(galleryText, ['parseGalleryTextArgs', 'parseArgs', 'parse']);
    assert.equal(typeof parse, 'function', 'gallery-text module must export an arg parser');
  });

  it('gallery-text-goldens module exports a callable entry point plus an arg parser', () => {
    assert.ok(galleryTextGoldens, 'lib/gallery-text-goldens.js must load');
    assert.equal(typeof callableEntry(galleryTextGoldens), 'function', 'gallery-text-goldens module must export a callable entry point');
    const parse = argParser(galleryTextGoldens, ['parseGalleryTextGoldensArgs', 'parseArgs', 'parse']);
    assert.equal(typeof parse, 'function', 'gallery-text-goldens module must export an arg parser');
  });

  it('gallery-text wrapper reuses the T5/T6/T3 gates (no reimplemented verification)', () => {
    assert.ok(galleryText, 'lib/gallery-text.js must load');
    const src = [galleryText.toString(), typeof galleryText === 'object' ? JSON.stringify(Object.keys(galleryText)) : ''].join('\n');
    assert.ok(
      /ShapedText|DrawText|atlas|cache|hit|bitmap|present_window|gallery_present|code-root|codeRoot/i.test(src),
      'gallery-text wrapper must reference the vector/atlas gate (thin reuse, not a reimplementation)',
    );
  });

  it('gallery-text-goldens wrapper shells the checked-in python gate with scripts/scales pass-through (stubbed, no spawn here)', () => {
    assert.ok(galleryTextGoldens, 'lib/gallery-text-goldens.js must load');
    const src = [galleryTextGoldens.toString(), typeof galleryTextGoldens === 'object' ? JSON.stringify(Object.keys(galleryTextGoldens)) : ''].join('\n');
    assert.ok(
      /text_goldens_check|acceptance/i.test(src),
      'gallery-text-goldens wrapper must reference the checked-in acceptance/text_goldens_check.py gate',
    );
    assert.ok(
      /scripts|scales/i.test(src),
      'gallery-text-goldens wrapper must pass --scripts/--scales through to the gate',
    );
  });

  it('gallery-text parser requires --assert-vector and --assert-atlas-reuse (usage failure, never silent)', () => {
    assert.ok(galleryText, 'lib/gallery-text.js must load');
    const parse = argParser(galleryText, ['parseGalleryTextArgs', 'parseArgs', 'parse']);
    assert.throws(() => parse([]), /assert-vector|assert-atlas-reuse|missing|required|Usage/i);
    assert.throws(() => parse(['--assert-vector']), /assert-atlas-reuse|missing|required|Usage/i);
  });

  it('gallery-text-goldens parser requires --scripts and --scales (usage failure, never silent)', () => {
    assert.ok(galleryTextGoldens, 'lib/gallery-text-goldens.js must load');
    const parse = argParser(galleryTextGoldens, ['parseGalleryTextGoldensArgs', 'parseArgs', 'parse']);
    assert.throws(() => parse([]), /scripts|scales|missing|required|Usage/i);
    assert.throws(() => parse(['--scripts', 'latin']), /scales|missing|required|Usage/i);
  });

  it('gallery-text-goldens parser round-trips --scripts/--scales values (gate arg pass-through)', () => {
    assert.ok(galleryTextGoldens, 'lib/gallery-text-goldens.js must load');
    const parse = argParser(galleryTextGoldens, ['parseGalleryTextGoldensArgs', 'parseArgs', 'parse']);
    const opts = parse(['--scripts', 'latin,cjk,emoji', '--scales', '1.0,1.3,2.0']);
    const flat = JSON.stringify(opts);
    assert.ok(/latin/.test(flat) && /1\.0/.test(flat), 'parsed opts must carry scripts/scales through to the python gate');
  });

  it('both parsers reject unknown flags (exit-2 usage shape)', () => {
    assert.ok(galleryText && galleryTextGoldens, 'both modules must load');
    const p1 = argParser(galleryText, ['parseGalleryTextArgs', 'parseArgs', 'parse']);
    const p2 = argParser(galleryTextGoldens, ['parseGalleryTextGoldensArgs', 'parseArgs', 'parse']);
    assert.throws(() => p1(['--assert-vector', '--assert-atlas-reuse', '--bogus-flag']), /bogus-flag|unknown/i);
    assert.throws(() => p2(['--scripts', 'latin', '--scales', '1.0', '--bogus-flag']), /bogus-flag|unknown/i);
  });
});

describe('T14 — gallery-text CLI exit contract (AC-TEXT-045, TC-TEXT-045 Check)', () => {
  it('exit 0 on the run checkout with --assert-vector --assert-atlas-reuse', () => {
    const res = runCli(['gallery-text', '--assert-vector', '--assert-atlas-reuse', '--code-root', CODE_ROOT]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 on the run checkout, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('nonzero with a per-sample delta report on the bitmap-path fixture (never silent)', () => {
    const root = mkTmp('muse-gtext-bitmap-');
    const srcDir = path.join(root, 'engine', 'runtime', 'src');
    fs.mkdirSync(srcDir, { recursive: true });
    fs.writeFileSync(
      path.join(srcDir, 'gallery_present.cpp'),
      ['// pre-fix bitmap path: gallery pages paint via the 5x7 bitmap fallback', 'void PaintCaption() { DrawText("hi"); }', ''].join('\n'),
    );
    const res = runCli(['gallery-text', '--assert-vector', '--assert-atlas-reuse', '--code-root', root]);
    assert.notEqual(res.status, 0, 'a bitmap-path tree must flip the vector check nonzero');
    assert.ok(
      /DrawText|bitmap|delta|diff|fail/i.test(`${res.stdout}\n${res.stderr}`),
      'failure output must carry a per-sample delta report naming the bitmap/vector gate, never a silent pass',
    );
  });

  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-text', '--assert-vector', '--assert-atlas-reuse', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`), 'stderr must name the unknown flag with usage');
  });

  it('missing flags exit 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-text', '--code-root', CODE_ROOT]);
    assert.equal(res.status, 2, `missing flags must exit 2, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 300)}`);
    assert.ok(/Usage/i.test(`${res.stdout}\n${res.stderr}`), 'missing-flag failure must print usage');
  });
});

describe('T14 — gallery-text-goldens CLI exit contract (AC-TEXT-046, TC-TEXT-046 Check)', () => {
  it('exit 0 on the run checkout with --scripts latin,cjk,emoji --scales 1.0,1.3,2.0', () => {
    const res = runCli(['gallery-text-goldens', '--scripts', 'latin,cjk,emoji', '--scales', '1.0,1.3,2.0', '--code-root', CODE_ROOT]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 on the run checkout, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('nonzero with a per-sample delta report on golden/sample mismatch (never silent)', () => {
    const root = mkTmp('muse-gtext-goldens-bad-');
    const dir = path.join(root, 'examples', 'm3-gallery', 'build', 'goldens');
    fs.mkdirSync(dir, { recursive: true });
    const png = Buffer.from([
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
      0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
      0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
      0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
      0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
      0x54, 0x08, 0xd7, 0x63, 0xf8, 0x00, 0x00, 0x00,
      0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc, 0x33,
      0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
      0xae, 0x42, 0x60, 0x82,
    ]);
    png[41] ^= 0xff;
    png[42] ^= 0xff;
    fs.writeFileSync(path.join(dir, 'text-latin-1.png'), png);
    fs.writeFileSync(path.join(dir, 'text-tolerances.json'), '{"exact":true}\n');
    const res = runCli(['gallery-text-goldens', '--scripts', 'latin', '--scales', '1.0', '--code-root', root]);
    assert.notEqual(res.status, 0, 'a golden/sample mismatch must flip the check nonzero');
    assert.ok(
      /delta|diff|mismatch|fail|text-latin/i.test(`${res.stdout}\n${res.stderr}`),
      'mismatch output must carry a per-sample delta report, never a silent pass',
    );
  });

  it('unknown flags exit 2 with usage naming the flag', () => {
    const res = runCli(['gallery-text-goldens', '--scripts', 'latin', '--scales', '1.0', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(/bogus-flag|Usage/i.test(`${res.stdout}\n${res.stderr}`), 'stderr must name the unknown flag with usage');
  });

  it('missing flags exit 2 with usage (never a silent pass)', () => {
    const res = runCli(['gallery-text-goldens', '--scripts', 'latin', '--code-root', CODE_ROOT]);
    assert.equal(res.status, 2, `missing --scales must exit 2, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 300)}`);
    assert.ok(/Usage/i.test(`${res.stdout}\n${res.stderr}`), 'missing-flag failure must print usage');
  });
});
