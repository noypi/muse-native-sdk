'use strict';
// T5 — Implement declared `gallery-scroll` audit CLI as thin hermetic wrapper
// (TC-LISTS-033 Check). Spec refs: FR-LISTS-007, AC-LISTS-033 (TC-LISTS-033 Check).
// Interface (plan verbatim):
//   `muse-native gallery-scroll --window-only --assert-no-full-layout [--rows N]`
//   — exit 0 on windowed-only paint, exit 1 naming the full-list layout/paint
//   site otherwise; hermetic (no device required); unknown flags exit 2 via
//   existing `OptionError`.
// Detail under test: per-command module `packages/cli/lib/gallery-scroll.js`
// required from `packages/cli/lib/cli.js` argv dispatch (`COMMANDS`
// extended); drives the windowed gallery path hermetically, counts painted
// rows vs realized window, asserts no full-list layout/paint on the UI
// thread.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path/child_process)
// only. Spawns `node bin/muse-native.js` against tmpdir fixtures (commands
// .test.js pattern); no device, no network, no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-S1: like the sibling renderer-prims/gallery-diff wrappers, the module
//   accepts an optional `--code-root <dir>` resolving the tree under audit
//   (default: the run checkout). The exit-1 negative is simulated with a
//   fixture code-root whose `engine/runtime/src/gallery_present.cpp` keeps a
//   full-list row loop (`for (int row = 0; ... rowCount ...)` painting every
//   row without windowing); the wrapper must exit 1 naming that site.
//   Coder owns detection internals; reconcile paths, not verdicts, at
//   implementation time.
// - A-S2: omitting `--window-only` or `--assert-no-full-layout`, or passing
//   a non-positive `--rows` value, exits nonzero (usage failure, never a
//   silent pass).

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

let galleryScroll = null;
try {
  // eslint-disable-next-line global-require
  galleryScroll = require('../lib/gallery-scroll.js');
} catch {
  galleryScroll = null;
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

// Fixture code-root whose gallery_present.cpp keeps the pre-fix full-list
// row loop (paints every row, no realized window). Never touches
// production code: a tmp tree only.
function writeFullListFixture(root) {
  const srcDir = path.join(root, 'engine', 'runtime', 'src');
  fs.mkdirSync(srcDir, { recursive: true });
  fs.writeFileSync(
    path.join(srcDir, 'gallery_present.cpp'),
    [
      '// pre-fix full-list path: lays out and paints every row on the UI thread',
      'void PaintGallery(int rowCount) {',
      '  for (int row = 0; row < rowCount; ++row) {',
      '    LayoutRow(row);',
      '    PaintRow(row);',
      '  }',
      '}',
      '',
    ].join('\n'),
  );
}

describe('T5 — gallery-scroll module surface (AC-LISTS-033)', () => {
  it('packages/cli/lib/gallery-scroll.js exists and loads', () => {
    assert.ok(galleryScroll !== null, 'lib/gallery-scroll.js must exist (thin wrapper module)');
  });

  it('wrapper drives the windowed gallery path (no reimplemented layout)', () => {
    assert.ok(galleryScroll, 'lib/gallery-scroll.js must load');
    const exported = typeof galleryScroll === 'function' ? galleryScroll : galleryScroll.run;
    assert.equal(typeof exported, 'function', 'gallery-scroll module must export a callable entry point');
    const src = [galleryScroll.toString(), typeof galleryScroll === 'object' ? JSON.stringify(Object.keys(galleryScroll)) : ''].join('\n');
    assert.ok(
      /window|realized|full-list|full_list|layout|paint|code-root|codeRoot/i.test(src),
      'wrapper must reference the realized window / full-list layout-paint gate (thin reuse, not a reimplementation)',
    );
  });
});

describe('T5 — gallery-scroll CLI exit contract (AC-LISTS-033, TC-LISTS-033 Check)', () => {
  it('exit 0 with --window-only --assert-no-full-layout on the fixed tree', () => {
    const res = runCli(['gallery-scroll', '--window-only', '--assert-no-full-layout', '--code-root', CODE_ROOT]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 on windowed-only paint, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('exit 0 with an explicit --rows N window size', () => {
    const res = runCli(['gallery-scroll', '--window-only', '--assert-no-full-layout', '--rows', '12', '--code-root', CODE_ROOT]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 with --rows 12, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('exit 1 naming the full-list layout/paint site on the pre-fix full-list path', () => {
    const root = mkTmp('muse-gscroll-fulllist-');
    writeFullListFixture(root);
    const res = runCli(['gallery-scroll', '--window-only', '--assert-no-full-layout', '--code-root', root]);
    assert.equal(res.status, 1, `full-list layout/paint must exit 1, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    assert.ok(
      /gallery_present\.cpp|full-list|full_list/i.test(`${res.stdout}\n${res.stderr}`),
      'exit-1 output must name the full-list layout/paint site',
    );
  });

  it('unknown flags exit 2 via OptionError naming the flag', () => {
    const res = runCli(['gallery-scroll', '--window-only', '--assert-no-full-layout', '--bogus-flag']);
    assert.equal(res.status, 2, `unknown flags must exit 2, got ${res.status}`);
    assert.ok(res.stderr.includes('--bogus-flag'), 'stderr must name the unknown flag');
  });

  it('nonzero when --assert-no-full-layout is omitted (never a silent pass)', () => {
    const res = runCli(['gallery-scroll', '--window-only', '--code-root', CODE_ROOT]);
    assert.notEqual(res.status, 0, 'omitting --assert-no-full-layout must not pass');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a usage failure, not an unknown command');
  });

  it('nonzero on a non-positive --rows value', () => {
    const res = runCli(['gallery-scroll', '--window-only', '--assert-no-full-layout', '--rows', '0', '--code-root', CODE_ROOT]);
    assert.notEqual(res.status, 0, 'a non-positive --rows value must not pass');
    assert.ok(!/Unknown command/.test(res.stderr), 'must be a usage failure, not an unknown command');
  });
});
