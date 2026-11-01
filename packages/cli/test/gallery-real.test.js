'use strict';
// Batch 2 (T3+T4) — `muse-native gallery-real` retained-component + interactive gates.
// Spec refs: FR-GRAPHICS-015, AC-GRAPHICS-042, AC-GRAPHICS-043, RULE-GRAPHICS-003,
//   TC-GRAPHICS-044, TC-GRAPHICS-045.
// Interfaces (plan.md verbatim):
//   T3: `sh engine/runtime/tests/gallery_page_no_oracle_check.sh <code-root>` — exit `0`
//     iff no mock/oracle-painter symbol (`PaintCaption`, oracle box painter) is reachable
//     from the gallery page target link set; exit nonzero + naming the offending symbol otherwise.
//   T4: `muse-native gallery-real --assert-retained-components [--code-root <dir>]` → exit `0`
//     + `gallery-real ok: retained-components` iff the T2 path emits retained-vocabulary cmds
//     with the T3 gate green; `muse-native gallery-real --assert-interactive [--code-root <dir>]`
//     → exit `0` + `gallery-real ok: interactive` iff tap/state/theme-repaint/IME-inset/layering
//     probes pass on the real-component pages; misuse (no flag, unknown flag) exits nonzero with
//     `Usage: muse-native gallery-real (--assert-retained-components|--assert-interactive) [--code-root <dir>]`.
// Hermetic: node:test + node:assert/strict + stdlib only. No device, no network, no secrets,
// no application config. The T3 shell gate is driven via spawnSync `sh` against temp fixture
// code-roots; the T4 CLI is driven via spawnSync `node bin/muse-native.js` plus an injectable
// `deps` override (ASSUMPTION A1) — hermetic cases never touch adb, a device, or live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `lib/gallery-real.js` exposes `parseGalleryRealArgs(argv)` and
//   `runGalleryReal(opts)` mirroring the `gallery-present.js` arg-parser + `runGalleryPresent`
//   shape. `opts` accepts `{ assertRetainedComponents, assertInteractive, codeRoot, deps }`
//   where `deps` is the hermetic seam `{ readT2Emission, runGate, runProbes }` plus an
//   optional `fs` double. The plan Interface line names only the CLI; the override is a
//   test-only seam, reconcile names (not verdicts) with the coder.
// - A2: T3 gate resolves the gallery page target sources under `<code-root>/engine/runtime`
//   (at minimum `src/gallery_present.cpp` plus the `CMakeLists.txt` page target). Temp
//   fixture code-roots mirror that layout; the real-checkout assertion uses `--code-root`
//   pointing at the checkout root.
// - A3: misuse maps to exit 2 with the exact plan Usage line on stderr (gallery-proof/
//   gallery-present exit-code convention 0/1/2); assertion failures map to exit 1 naming
//   the failing probe or symbol.
// - A4: `--code-root` defaults to process cwd when omitted.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const CHECKOUT_ROOT = path.resolve(__dirname, '..', '..', '..');
const GATE_REL = path.join('engine', 'runtime', 'tests', 'gallery_page_no_oracle_check.sh');
const GATE_ABS = path.join(CHECKOUT_ROOT, GATE_REL);
const EXPECTED_USAGE =
  'Usage: muse-native gallery-real (--assert-retained-components|--assert-interactive) [--code-root <dir>]';

let galleryReal = null;
try {
  // eslint-disable-next-line global-require
  galleryReal = require('../lib/gallery-real.js');
} catch {
  galleryReal = null;
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function runGate(codeRoot, extraArgs = []) {
  return spawnSync('sh', [GATE_ABS, codeRoot, ...extraArgs], { encoding: 'utf8' });
}

function mkCodeRoot() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-gallery-real-'));
  fs.mkdirSync(path.join(root, 'engine', 'runtime', 'src'), { recursive: true });
  fs.mkdirSync(path.join(root, 'engine', 'runtime', 'tests'), { recursive: true });
  return root;
}

function writePageTarget(root, { galleryPresentCpp, cmakeLists }) {
  if (galleryPresentCpp !== undefined) {
    fs.writeFileSync(path.join(root, 'engine', 'runtime', 'src', 'gallery_present.cpp'), galleryPresentCpp);
  }
  if (cmakeLists !== undefined) {
    fs.writeFileSync(path.join(root, 'engine', 'runtime', 'CMakeLists.txt'), cmakeLists);
  }
}

// Minimal clean page-target stub: component-record path only, no oracle painter.
function cleanPageSource() {
  return [
    '#include "runtime/gallery_present.h"',
    'bool PresentRealGalleryPage(const std::vector<GalleryRow>& rows, int page,',
    '                            runtime::Renderer& renderer, runtime::Swapchain& target,',
    '                            std::string* error) {',
    '  // component-record path: retained 11-kind vocabulary via renderer.draw only',
    '  return renderer.draw({}, target) == runtime::DrawError::None;',
    '}',
    '',
  ].join('\n');
}

// Tainted stub: oracle painter reachable from the page path.
function taintedPageSource(symbol = 'PaintCaption') {
  return [
    '#include "runtime/gallery_present.h"',
    `void ${symbol}(void) {}`,
    'bool PresentRealGalleryPage(const std::vector<GalleryRow>& rows, int page,',
    '                            runtime::Renderer& renderer, runtime::Swapchain& target,',
    '                            std::string* error) {',
    `  ${symbol}(); // oracle painter reachable from the page target`,
    '  return true;',
    '}',
    '',
  ].join('\n');
}

describe('T3 — oracle gate script surface (RULE-GRAPHICS-003, AC-GRAPHICS-042)', () => {
  it('engine/runtime/tests/gallery_page_no_oracle_check.sh exists and is executable', () => {
    assert.ok(fs.existsSync(GATE_ABS), `${GATE_REL} must exist`);
    try {
      fs.accessSync(GATE_ABS, fs.constants.X_OK);
    } catch {
      assert.fail(`${GATE_REL} must be executable`);
    }
  });

  it('clean fixture code-root exits 0 (no oracle symbol reachable)', () => {
    const root = mkCodeRoot();
    try {
      writePageTarget(root, { galleryPresentCpp: cleanPageSource(), cmakeLists: 'add_library(runtime src/gallery_present.cpp)\n' });
      const res = runGate(root);
      assert.equal(res.status, 0,
        `clean gate must exit 0, got ${res.status}; stdout: ${(res.stdout || '').slice(0, 400)} stderr: ${(res.stderr || '').slice(0, 400)}`);
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });

  it('PaintCaption reachable from the page target exits nonzero naming PaintCaption', () => {
    const root = mkCodeRoot();
    try {
      writePageTarget(root, { galleryPresentCpp: taintedPageSource('PaintCaption'), cmakeLists: 'add_library(runtime src/gallery_present.cpp)\n' });
      const res = runGate(root);
      assert.notEqual(res.status, 0, 'tainted gate must exit nonzero');
      assert.ok(/PaintCaption/.test(`${res.stdout}\n${res.stderr}`),
        'tainted failure must name the offending symbol PaintCaption');
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });

  it('oracle box painter reachable exits nonzero naming that symbol', () => {
    const root = mkCodeRoot();
    try {
      writePageTarget(root, { galleryPresentCpp: taintedPageSource('PaintOracleBox'), cmakeLists: 'add_library(runtime src/gallery_present.cpp)\n' });
      const res = runGate(root);
      assert.notEqual(res.status, 0, 'oracle-box taint must exit nonzero');
      assert.ok(/PaintOracleBox/.test(`${res.stdout}\n${res.stderr}`),
        'oracle-box failure must name the offending symbol');
    } finally {
      fs.rmSync(root, { recursive: true, force: true });
    }
  });

  it('missing <code-root> exits nonzero with usage (never a silent pass)', () => {
    const res = spawnSync('sh', [GATE_ABS], { encoding: 'utf8' });
    assert.notEqual(res.status, 0, 'missing code-root must exit nonzero');
    assert.ok(/Usage|code-root/i.test(`${res.stdout}\n${res.stderr}`),
      'missing code-root failure must print usage');
  });

  it('nonexistent <code-root> exits nonzero (never a silent pass)', () => {
    const missing = path.join(os.tmpdir(), `muse-no-such-root-${Date.now()}`);
    const res = runGate(missing);
    assert.notEqual(res.status, 0, 'nonexistent code-root must exit nonzero');
  });
});

describe('T4 — gallery-real module surface (FR-GRAPHICS-015)', () => {
  it('packages/cli/lib/gallery-real.js exists and loads', () => {
    assert.ok(galleryReal !== null, 'lib/gallery-real.js must exist');
  });

  it('module exports parseGalleryRealArgs', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    assert.equal(typeof galleryReal.parseGalleryRealArgs, 'function',
      'must export parseGalleryRealArgs(argv)');
  });

  it('module exports runGalleryReal', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    assert.equal(typeof galleryReal.runGalleryReal, 'function',
      'must export runGalleryReal(opts)');
  });
});

describe('T4 — parseGalleryRealArgs hermetic (no device)', () => {
  it('parses --assert-retained-components with no code-root (defaults to cwd)', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    const parsed = galleryReal.parseGalleryRealArgs(['gallery-real', '--assert-retained-components']);
    assert.equal(parsed.assertRetainedComponents, true);
    assert.equal(parsed.assertInteractive, false);
  });

  it('parses --assert-interactive --code-root <dir>', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    const parsed = galleryReal.parseGalleryRealArgs(
      ['gallery-real', '--assert-interactive', '--code-root', '/tmp/x']);
    assert.equal(parsed.assertInteractive, true);
    assert.equal(parsed.assertRetainedComponents, false);
    assert.equal(parsed.codeRoot, '/tmp/x');
  });

  it('parses without the leading gallery-real token', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    const parsed = galleryReal.parseGalleryRealArgs(['--assert-retained-components']);
    assert.equal(parsed.assertRetainedComponents, true);
  });

  it('bare gallery-real with no flag throws usage (never a silent pass)', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    assert.throws(() => galleryReal.parseGalleryRealArgs(['gallery-real']), /Usage: muse-native gallery-real/);
  });

  it('unknown flag throws naming the flag with usage', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    assert.throws(
      () => galleryReal.parseGalleryRealArgs(['gallery-real', '--bogus-flag']),
      /bogus-flag|unknown/i);
  });

  it('missing --code-root value throws usage (never a silent pass)', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    assert.throws(
      () => galleryReal.parseGalleryRealArgs(['gallery-real', '--assert-retained-components', '--code-root']),
      /code-root|value|Usage/i);
  });

  it('both assert flags together throw usage (mutually exclusive)', () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    assert.throws(
      () => galleryReal.parseGalleryRealArgs(
        ['gallery-real', '--assert-retained-components', '--assert-interactive']),
      /Usage|exclusive|one/i);
  });
});

describe('T4 — runGalleryReal hermetic, T2/gate/probes stubbed (no device)', () => {
  it('retained-components resolves ok when T2 emits retained vocabulary and the gate is green', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    const result = await galleryReal.runGalleryReal({
      assertRetainedComponents: true,
      codeRoot: CHECKOUT_ROOT,
      deps: {
        readT2Emission: async () => ({ kinds: ['Rect', 'Text', 'StateLayer'], oraclePixels: 0 }),
        runGate: async () => ({ ok: true }),
      },
    });
    assert.ok(/retained-components/.test(result.message || result.ok || JSON.stringify(result)),
      'retained-components success must carry the retained-components marker');
  });

  it('retained-components rejects when the T3 gate is red (naming the symbol)', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    await assert.rejects(
      galleryReal.runGalleryReal({
        assertRetainedComponents: true,
        codeRoot: CHECKOUT_ROOT,
        deps: {
          readT2Emission: async () => ({ kinds: ['Rect'], oraclePixels: 0 }),
          runGate: async () => ({ ok: false, symbol: 'PaintCaption' }),
        },
      }),
      /PaintCaption|gate|oracle/i);
  });

  it('retained-components rejects on oracle pixels (AC-GRAPHICS-042, zero oracle output)', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    await assert.rejects(
      galleryReal.runGalleryReal({
        assertRetainedComponents: true,
        codeRoot: CHECKOUT_ROOT,
        deps: {
          readT2Emission: async () => ({ kinds: ['Rect'], oraclePixels: 7 }),
          runGate: async () => ({ ok: true }),
        },
      }),
      /oracle/i);
  });

  it('retained-components rejects a non-retained kind (fail-closed vocabulary)', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    await assert.rejects(
      galleryReal.runGalleryReal({
        assertRetainedComponents: true,
        codeRoot: CHECKOUT_ROOT,
        deps: {
          readT2Emission: async () => ({ kinds: ['Rect', 'BogusKind'], oraclePixels: 0 }),
          runGate: async () => ({ ok: true }),
        },
      }),
      /BogusKind|vocabulary|retained/i);
  });

  it('interactive resolves ok when tap/state/theme/IME/layering probes pass', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    const result = await galleryReal.runGalleryReal({
      assertInteractive: true,
      codeRoot: CHECKOUT_ROOT,
      deps: {
        runProbes: async () => ({
          tap: true, state: true, themeRepaint: true, imeInset: true, layering: true,
        }),
      },
    });
    assert.ok(/interactive/.test(result.message || result.ok || JSON.stringify(result)),
      'interactive success must carry the interactive marker');
  });

  it('interactive rejects naming the failing probe (theme repaint)', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    await assert.rejects(
      galleryReal.runGalleryReal({
        assertInteractive: true,
        codeRoot: CHECKOUT_ROOT,
        deps: {
          runProbes: async () => ({
            tap: true, state: true, themeRepaint: false, imeInset: true, layering: true,
          }),
        },
      }),
      /theme/i);
  });

  it('interactive rejects naming the failing probe (IME inset)', async () => {
    assert.ok(galleryReal, 'lib/gallery-real.js must load');
    await assert.rejects(
      galleryReal.runGalleryReal({
        assertInteractive: true,
        codeRoot: CHECKOUT_ROOT,
        deps: {
          runProbes: async () => ({
            tap: true, state: true, themeRepaint: true, imeInset: false, layering: true,
          }),
        },
      }),
      /ime|inset/i);
  });
});

describe('T4 — gallery-real CLI exit contract (TC-GRAPHICS-044, TC-GRAPHICS-045 Checks)', () => {
  it('bare gallery-real (no flag) exits nonzero with the exact plan Usage line', () => {
    const res = runCli(['gallery-real']);
    assert.notEqual(res.status, 0, `misuse must exit nonzero, got ${res.status}`);
    assert.ok(`${res.stdout}\n${res.stderr}`.includes(EXPECTED_USAGE),
      `misuse must print exactly: ${EXPECTED_USAGE}`);
  });

  it('unknown flag exits nonzero with the exact plan Usage line', () => {
    const res = runCli(['gallery-real', '--bogus-flag']);
    assert.notEqual(res.status, 0, `unknown flag must exit nonzero, got ${res.status}`);
    assert.ok(`${res.stdout}\n${res.stderr}`.includes(EXPECTED_USAGE),
      `unknown-flag failure must print exactly: ${EXPECTED_USAGE}`);
  });

  it('--assert-retained-components exits 0 with `gallery-real ok: retained-components` on this tree', () => {
    const res = runCli(['gallery-real', '--assert-retained-components', '--code-root', CHECKOUT_ROOT]);
    assert.equal(res.status, 0,
      `expected exit 0, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    assert.ok(/gallery-real ok: retained-components/.test(`${res.stdout}\n${res.stderr}`),
      'success must print `gallery-real ok: retained-components`');
  });

  it('--assert-interactive exits 0 with `gallery-real ok: interactive` on this tree', () => {
    const res = runCli(['gallery-real', '--assert-interactive', '--code-root', CHECKOUT_ROOT]);
    assert.equal(res.status, 0,
      `expected exit 0, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`);
    assert.ok(/gallery-real ok: interactive/.test(`${res.stdout}\n${res.stderr}`),
      'success must print `gallery-real ok: interactive`');
  });
});
