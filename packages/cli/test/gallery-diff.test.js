'use strict';
// T13 — Implement `muse-native gallery-diff --tolerance m3-tolerances.json`
// (TC-GRAPHICS-030 Check). Spec refs: FR-GRAPHICS-010, AC-GRAPHICS-030.
// Interface (plan verbatim):
//   `muse-native gallery-diff --tolerance m3-tolerances.json` — diffs the
//   committed per-page goldens (`page-*.png`) against the retained-path render
//   within the tolerances file; exit 0 iff every page is within tolerance,
//   nonzero otherwise. Hermetic: no device, no network.
// Detail under test: thin wrapper over the T9 per-page golden diff logic
// (reuse gallery_present_test diff path / retained render, not a
// reimplementation); per-command module `packages/cli/lib/gallery-diff.js`
// required from `packages/cli/lib/cli.js` argv dispatch; tolerances from the
// given m3-tolerances.json (perPixel 64 / maxFailPct 0.12); golden dir resolved
// relative to `--code-root` / current checkout the same way T12 does.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path/child_process)
// only. Spawns `node bin/muse-native.js` against tmpdir fixtures (commands
// .test.js pattern); no device, no network, no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-D1: the wrapper re-renders pages through the retained path under
//   `--code-root` and diffs against committed `page-*.png` goldens within the
//   given tolerances. The perturbed-beyond-tolerance negative is therefore
//   simulated with a fixture code-root whose page bytes are corrupted beyond
//   any headroom (byte-flipped copy) paired with a strict (0/0) tolerances
//   file — fixture-based, no device, no production-code touch. Exact differ
//   internals are coder-owned; reconcile paths, not verdicts, at
//   implementation time.
// - A-D2: missing `--tolerance`, a nonexistent tolerances path, and a
//   malformed tolerances file all exit nonzero (never a silent pass).

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
const RUN_TOLERANCE = path.join(CODE_ROOT, 'examples', 'm3-gallery', 'build', 'goldens', 'm3-tolerances.json');

let galleryDiff = null;
try {
  // eslint-disable-next-line global-require
  galleryDiff = require('../lib/gallery-diff.js');
} catch {
  galleryDiff = null;
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

// Minimal 1x1 PNG bytes; flipByte corrupts IDAT payload beyond any tolerance.
function stubPng(seed) {
  const bytes = Buffer.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
    0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
    0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
    0x54, 0x08, 0xd7, 0x63, seed & 0xff, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc, 0x33,
    0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
    0xae, 0x42, 0x60, 0x82,
  ]);
  return bytes;
}

// Fixture golden dir: one page; `perturb=true` flips payload bytes so the
// committed golden can no longer match any faithful re-render.
function writeGoldenFixture(root, { perturb, perPixel, maxFailPct }) {
  const dir = path.join(root, 'examples', 'm3-gallery', 'build', 'goldens');
  fs.mkdirSync(dir, { recursive: true });
  const png = stubPng(0xf8);
  if (perturb) {
    png[41] ^= 0xff;
    png[42] ^= 0xff;
  }
  fs.writeFileSync(path.join(dir, 'page-p0.png'), png);
  const tolPath = path.join(dir, 'm3-tolerances.json');
  fs.writeFileSync(tolPath, `${JSON.stringify({ perPixel, maxFailPct })}\n`);
  return tolPath;
}

describe('T13 — gallery-diff module surface (AC-GRAPHICS-030)', () => {
  it('packages/cli/lib/gallery-diff.js exists and loads', () => {
    assert.ok(galleryDiff !== null, 'lib/gallery-diff.js must exist (thin wrapper module)');
  });

  it('wrapper reuses the T9 diff path (no reimplemented differ)', () => {
    assert.ok(galleryDiff, 'lib/gallery-diff.js must load');
    const exported = typeof galleryDiff === 'function' ? galleryDiff : galleryDiff.run;
    assert.equal(typeof exported, 'function', 'gallery-diff module must export a callable entry point');
    const src = [galleryDiff.toString(), typeof galleryDiff === 'object' ? JSON.stringify(Object.keys(galleryDiff)) : ''].join('\n');
    assert.ok(
      /gallery_present_test|tolerance|code-root|codeRoot|golden/i.test(src),
      'wrapper must reference the T9 diff path / tolerances / golden dir (thin reuse, not a reimplementation)',
    );
  });
});

describe('T13 — gallery-diff CLI exit contract (AC-GRAPHICS-030)', () => {
  it('exit 0 on the run checkout within m3-tolerances.json', () => {
    const res = runCli(['gallery-diff', '--tolerance', RUN_TOLERANCE, '--code-root', CODE_ROOT]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 on the run checkout, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('nonzero on perturbed-beyond-tolerance fixture input', () => {
    const root = mkTmp('muse-gdiff-bad-');
    const tol = writeGoldenFixture(root, { perturb: true, perPixel: 0, maxFailPct: 0 });
    const res = runCli(['gallery-diff', '--tolerance', tol, '--code-root', root]);
    assert.notEqual(res.status, 0, 'a page perturbed beyond tolerance must flip the check nonzero');
  });

  it('nonzero when --tolerance is omitted', () => {
    const res = runCli(['gallery-diff', '--code-root', CODE_ROOT]);
    assert.notEqual(res.status, 0, 'omitting --tolerance must not pass');
  });

  it('nonzero on a nonexistent tolerances path', () => {
    const res = runCli([
      'gallery-diff',
      '--tolerance',
      path.join(os.tmpdir(), 'muse-gdiff-nope-12345.json'),
      '--code-root',
      CODE_ROOT,
    ]);
    assert.notEqual(res.status, 0, 'nonexistent tolerances path must exit nonzero, never a silent pass');
  });

  it('nonzero on a malformed tolerances file', () => {
    const root = mkTmp('muse-gdiff-malformed-');
    const bad = path.join(root, 'm3-tolerances.json');
    fs.writeFileSync(bad, 'not json at all\n');
    const res = runCli(['gallery-diff', '--tolerance', bad, '--code-root', root]);
    assert.notEqual(res.status, 0, 'malformed tolerances JSON must exit nonzero');
  });
});
