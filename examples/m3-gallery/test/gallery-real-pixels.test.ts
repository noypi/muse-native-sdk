'use strict';
// FR-M3-011 Batch 3 — T5 contract.
//
// Re-pass theme/Dynamic Color (AC-M3-009/010), IME-inset follow (AC-M3-013),
// clipboard-adjacent error-state (AC-M3-014), layering z-order/scrim/IME-safe
// sheet (AC-M3-022) proof on real-component pages; assert no blank viewport;
// diff against goldens with recorded tolerance; block re-baseline while red.
// TC-M3-028: `muse-native gallery-real --assert-real-pixels`.
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only.
// No network, no secrets, no application configuration, no device, no adb,
// no live service, no shared mutable state. `process.env` is never read.
// Device pixels and on-device runs are EXTERNAL (tester/device) — the unit
// tests pin the gate shape (report fields, tolerance file, blank-viewport
// assert, re-baseline block), never pixels.
//
// Binds ONLY to the plan interface (reproduced verbatim in
// unit-tests/T5.md):
//   `runRealPixelProof(theme: "light" | "dark"): PixelProofReport — returns PixelProofReport{blankViewports: number, goldenDiffs: number, toleranceFile: string}`
//
// ASSUMPTIONS (the plan Interface is genuinely silent on module spelling —
// the coder reconciles layout per GR-08 T1.9; behavior pinned, spelling
// reconcilable):
//   A1 (module path): tests probe `examples/m3-gallery/src/gallery-proof.js`
//       first (the plan's Files entry; owns isBlankViewport/
//       diffAgainstGolden/toleranceFor/TOLERANCES_PATH already), then
//       `packages/cli/lib/gallery-real.js`, then
//       `packages/cli/src/gallery-real.js`. Any location exporting
//       `runRealPixelProof` satisfies the contract.
//   A2 (report shape): PixelProofReport is exactly
//       `{blankViewports: number, goldenDiffs: number, toleranceFile: string}`;
//       counts are integers >= 0, toleranceFile is a path string ending in
//       `m3-tolerances.json` that resolves to the committed goldens file.
//   A3 (fail-closed theme): a theme outside `"light" | "dark"` (unknown,
//       empty, blank, null, undefined, non-string, oversized) throws an
//       Error instead of returning a report; a 1MB input rejects fast.
//   A4 (pixel scope): per-pixel magnitudes, golden image bytes, and the
//       on-device screencap loop are device-lane proof (EXTERNAL, tester via
//       `gallery-real --assert-real-pixels` on device). The unit gate pins
//       that blank viewports are counted, golden diffs are counted against
//       the recorded tolerance file, and re-baseline is refused while red.
//   A5 (CLI flag): the real-pixel gate is wired into the `gallery-real` CLI
//       `--assert-real-pixels` flag next to `--assert-no-diagram` /
//       `--assert-interactive` in `packages/cli/lib/gallery-real.js`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join, resolve, basename } = require('node:path');

const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const PROOF_CANDIDATES = [
  join(__dirname, '..', 'src', 'gallery-proof.js'),
  join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-real.js'),
  join(REPO_ROOT, 'packages', 'cli', 'src', 'gallery-real.js'),
];
const CLI_LIB = join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-real.js');
const GOLDENS_TOLERANCES = join(__dirname, '..', 'goldens', 'm3-tolerances.json');

const REPORT_FIELDS = ['blankViewports', 'goldenDiffs', 'toleranceFile'];

function loadRunRealPixel() {
  for (const path of PROOF_CANDIDATES) {
    try {
      // eslint-disable-next-line import/no-dynamic-require, global-require
      const mod = require(path);
      if (mod && typeof mod.runRealPixelProof === 'function') {
        return { fn: mod.runRealPixelProof, path, error: null };
      }
    } catch {
      // Candidate missing or unparsable — try the next one.
    }
  }
  return { fn: null, path: null, error: new Error('module must export runRealPixelProof(theme)') };
}

function loadProofSource() {
  for (const path of PROOF_CANDIDATES) {
    try {
      return { text: readFileSync(path, 'utf8'), path };
    } catch {
      // Try the next candidate.
    }
  }
  return { text: null, path: null };
}

function requireFn() {
  const loaded = loadRunRealPixel();
  assert.ok(loaded.fn, `runRealPixelProof export missing (probed ${PROOF_CANDIDATES.join(', ')})`);
  return loaded.fn;
}

function assertReportShape(report, theme) {
  assert.equal(typeof report, 'object', `report for theme ${theme} must be an object`);
  assert.deepEqual(Object.keys(report).sort(), [...REPORT_FIELDS].sort(), `report for theme ${theme} must carry exactly ${REPORT_FIELDS.join('/')}`);
  assert.equal(typeof report.blankViewports, 'number', 'blankViewports must be a number');
  assert.equal(typeof report.goldenDiffs, 'number', 'goldenDiffs must be a number');
  assert.ok(Number.isInteger(report.blankViewports) && report.blankViewports >= 0, 'blankViewports must be an integer >= 0');
  assert.ok(Number.isInteger(report.goldenDiffs) && report.goldenDiffs >= 0, 'goldenDiffs must be an integer >= 0');
  assert.equal(typeof report.toleranceFile, 'string', 'toleranceFile must be a string');
  assert.ok(report.toleranceFile.length > 0, 'toleranceFile must be non-empty');
  assert.equal(basename(report.toleranceFile), 'm3-tolerances.json', 'toleranceFile must name m3-tolerances.json');
}

describe('T5 gallery-real-pixels proof gate (AC-M3-027 / TC-M3-028)', () => {
  it('loads a proof module candidate', () => {
    const src = loadProofSource();
    assert.ok(src.text !== null, `no proof candidate loads (probed ${PROOF_CANDIDATES.join(', ')})`);
  });

  it('exports runRealPixelProof(theme)', () => {
    requireFn();
  });

  it('light theme returns a PixelProofReport', () => {
    const report = requireFn()('light');
    assertReportShape(report, 'light');
  });

  it('dark theme returns a PixelProofReport', () => {
    const report = requireFn()('dark');
    assertReportShape(report, 'dark');
  });

  it('toleranceFile resolves to the committed goldens tolerance file', () => {
    const report = requireFn()('light');
    const resolved = resolve(REPO_ROOT, report.toleranceFile);
    const direct = resolve(report.toleranceFile);
    assert.ok(
      existsSync(resolved) || existsSync(direct) || existsSync(GOLDENS_TOLERANCES),
      `toleranceFile must resolve to a committed file (saw ${report.toleranceFile})`,
    );
    assert.ok(existsSync(GOLDENS_TOLERANCES), 'committed goldens/m3-tolerances.json must exist');
  });

  it('counts blank viewports instead of passing silently', () => {
    const loaded = loadRunRealPixel();
    assert.ok(loaded.fn, 'runRealPixelProof export missing — no real-pixel path counts blank viewports yet');
    const text = readFileSync(loaded.path, 'utf8');
    assert.ok(
      /blankViewport/i.test(text),
      'real-pixel path must count blank viewports (no blankViewport reference found)',
    );
    const report = loaded.fn('light');
    assert.equal(typeof report.blankViewports, 'number', 'blankViewports must be counted on the report');
  });

  it('diffs against goldens with the recorded tolerance', () => {
    const loaded = loadRunRealPixel();
    assert.ok(loaded.fn, 'runRealPixelProof export missing — no real-pixel path diffs against goldens yet');
    const text = readFileSync(loaded.path, 'utf8');
    assert.ok(
      /diffAgainstGolden|goldenDiff|toleranceFor|TOLERANCES_PATH/.test(text),
      'real-pixel path must diff against goldens with the recorded tolerance file',
    );
    const report = loaded.fn('dark');
    assert.equal(typeof report.goldenDiffs, 'number', 'goldenDiffs must be counted on the report');
  });

  it('covers both light and dark themes in the real-pixel path', () => {
    const loaded = loadRunRealPixel();
    assert.ok(loaded.fn, 'runRealPixelProof export missing — no real-pixel path covers light/dark yet');
    const text = readFileSync(loaded.path, 'utf8');
    assert.ok(/light/.test(text) && /dark/.test(text), 'real-pixel path must name both light and dark themes');
    loaded.fn('light');
    loaded.fn('dark');
  });

  it('blocks golden re-baseline while the gate is red', () => {
    const loaded = loadRunRealPixel();
    assert.ok(loaded.fn, 'runRealPixelProof export missing — no gate enforces the re-baseline block yet');
    const text = readFileSync(loaded.path, 'utf8');
    assert.ok(/re-?baseline/i.test(text), 'real-pixel gate must name the re-baseline block');
    assert.ok(
      /re-?baseline[^]{0,800}blankViewport|blankViewport[^]{0,800}re-?baseline|re-?baseline[^]{0,800}goldenDiff|goldenDiff[^]{0,800}re-?baseline|while red|until green|refus[^]{0,40}re-?baseline/i.test(text),
      're-baseline must be refused while blankViewports or goldenDiffs are non-zero',
    );
  });

  it('rejects an unknown theme fail-closed', () => {
    assert.throws(() => requireFn()('sepia'), /theme/i, 'unknown theme must throw');
  });

  it('rejects empty/blank themes fail-closed', () => {
    assert.throws(() => requireFn()(''), /theme/i, 'empty theme must throw');
    assert.throws(() => requireFn()('   '), /theme/i, 'blank theme must throw');
  });

  it('rejects null/undefined/non-string themes fail-closed', () => {
    assert.throws(() => requireFn()(null), /theme/i, 'null theme must throw');
    assert.throws(() => requireFn()(undefined), /theme/i, 'undefined theme must throw');
    assert.throws(() => requireFn()(42), /theme/i, 'non-string theme must throw');
  });

  it('rejects an oversized theme input fast', () => {
    const big = `light${'x'.repeat(1024 * 1024)}`;
    const start = Date.now();
    assert.throws(() => requireFn()(big), /theme/i, 'oversized theme must throw');
    assert.ok(Date.now() - start < 5000, 'oversized theme must reject in well under 5s');
  });

  it('denial errors are Error instances with a message', () => {
    const loaded = loadRunRealPixel();
    assert.ok(loaded.fn, 'runRealPixelProof export missing — no denial shape to check yet');
    try {
      loaded.fn('no-such-theme');
      assert.fail('unknown theme must throw');
    } catch (err) {
      assert.ok(err instanceof Error, 'denial must be an Error');
      assert.ok(String(err.message).length > 0, 'denial must carry a message');
      assert.ok(!/mockReachable|PaintCaption/i.test(String(err.message)), 'denial must not leak unrelated internals');
    }
  });

  it('gallery-real CLI parses --assert-real-pixels', () => {
    assert.ok(existsSync(CLI_LIB), `CLI module must exist at ${CLI_LIB}`);
    const text = readFileSync(CLI_LIB, 'utf8');
    assert.ok(/assert-real-pixels/.test(text), "CLI must wire the '--assert-real-pixels' flag (TC-M3-028)");
  });

  it('committed tolerance file records per-component values', () => {
    assert.ok(existsSync(GOLDENS_TOLERANCES), 'goldens/m3-tolerances.json must exist');
    const parsed = JSON.parse(readFileSync(GOLDENS_TOLERANCES, 'utf8'));
    assert.equal(typeof parsed, 'object', 'tolerances must be an object');
    assert.ok(typeof parsed.default === 'number', 'tolerances must record a default value');
  });
});
