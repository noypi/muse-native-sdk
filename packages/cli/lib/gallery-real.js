'use strict';
// Merged `lib/gallery-real.js` (resolve-conflicts round 2): serves BOTH the
// FR-GRAPHICS-015 T4 retained-component/interactive source gates
// (`--assert-retained-components|--assert-interactive [--code-root <dir>]`)
// AND the upstream FR-M3-011 no-diagram/counts/real-pixels/page dispatch
// (`assertNoMockPainter`, `runInteractiveAssertions`, M3 arg parser/runner).
// T4 keeps the canonical `parseGalleryRealArgs` / `runGalleryReal` names
// (bare/unknown misuse throws T4 usage); the M3 surface lives under the
// `*M3*` aliases plus `assertNoMockPainter` / `runInteractiveAssertions`.
// gallery-real retained-component + interactive gates — FR-GRAPHICS-015 T4
// (AC-GRAPHICS-042, AC-GRAPHICS-043, TC-GRAPHICS-044, TC-GRAPHICS-045).
//
// `muse-native gallery-real --assert-retained-components [--code-root <dir>]`
// exits 0 with `gallery-real ok: retained-components` iff the T2 path emits
// retained-vocabulary cmds with the T3 gate green; `--assert-interactive`
// exits 0 with `gallery-real ok: interactive` iff the tap/state/
// theme-repaint/IME-inset/layering probes pass on the real-component pages.
// Misuse (no flag, unknown flag) exits nonzero with the plan Usage line.
// Both flags read the T2/T3 sources under `--code-root` (default: process
// cwd). No device, no network. Mirrors the `gallery-present.js` arg-parser +
// `runGalleryPresent` shape; `deps` is the hermetic test seam
// `{ readT2Emission, runGate, runProbes }` plus an optional `fs` double.

const fs = require('node:fs');
const path = require('node:path');

const USAGE =
  'Usage: muse-native gallery-real (--assert-retained-components|--assert-interactive) [--code-root <dir>]';

// The retained 11-kind DrawCmd vocabulary (`runtime/renderer.h`
// `PrimitiveKind`). The T2 component-record path may emit only these.
const RETAINED_VOCABULARY = [
  'Rect',
  'RoundedRect',
  'Image',
  'Text',
  'StateLayer',
  'Ripple',
  'ElevationShadow',
  'CircleArc',
  'VectorIcon',
  'ShapedText',
  'OverlayScrim',
];

const ORACLE_SYMBOLS = ['PaintCaption', 'PaintOracleBox', 'RasterGalleryPage'];

// Parse `muse-native gallery-real (--assert-retained-components|
// --assert-interactive) [--code-root <dir>]`. The leading `gallery-real`
// token is optional. Unknown flags, a missing `--code-root` value, a bare
// invocation, or both assert flags together throw (the CLI maps these to
// exit 2 with usage, never a silent pass).
function parseGalleryRealT4Args(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-real') args.shift();
  const parsed = {
    assertRetainedComponents: false,
    assertInteractive: false,
    codeRoot: process.cwd(),
  };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--assert-retained-components') {
      parsed.assertRetainedComponents = true;
    } else if (arg === '--assert-interactive') {
      parsed.assertInteractive = true;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error(`Error: --code-root: Expected a value <dir>\n${USAGE}`);
      }
      parsed.codeRoot = next;
      i++;
    } else {
      throw new Error(`Error: unknown flag ${arg}\n${USAGE}`);
    }
  }
  if (parsed.assertRetainedComponents && parsed.assertInteractive) {
    throw new Error(
      `Error: --assert-retained-components and --assert-interactive are mutually exclusive (choose one)\n${USAGE}`,
    );
  }
  if (!parsed.assertRetainedComponents && !parsed.assertInteractive) {
    throw new Error(`Error: missing required flag --assert-retained-components|--assert-interactive\n${USAGE}`);
  }
  return parsed;
}

// Extract the device-frame page entry bodies (PresentRealGalleryPage plus
// every PresentDeviceGalleryPage overload) from the T2 source: from each
// page-entry signature line to the closing `}` at column 0. Mirrors the
// `awk` extraction in `engine/runtime/tests/gallery_page_no_oracle_check.sh`.
function extractPageBody(source) {
  const lines = String(source).split('\n');
  const out = [];
  let inBody = false;
  for (const line of lines) {
    if (!inBody && (/^bool PresentRealGalleryPage\(/.test(line) || /^bool PresentDeviceGalleryPage\(/.test(line))) {
      inBody = true;
    }
    if (inBody) {
      out.push(line);
      if (/^\}/.test(line)) inBody = false;
    }
  }
  return out.join('\n');
}

// Default T2 emission read: scan the page entry bodies under `codeRoot` for
// oracle-painter reachability and retained-vocabulary emission markers.
// Rejects (fail-closed) when the T2 source is missing or unreadable.
async function defaultReadT2Emission({ codeRoot, fs: fsDouble } = {}) {
  const reader = fsDouble || fs;
  const root = codeRoot || process.cwd();
  const src = path.join(root, 'engine', 'runtime', 'src', 'gallery_present.cpp');
  let text;
  try {
    text = reader.readFileSync(src, 'utf8');
  } catch (err) {
    throw new Error(`gallery-real: cannot read T2 page source: ${src}: ${(err && err.message) || String(err)}`);
  }
  const body = extractPageBody(text);
  if (!body || body.length === 0) {
    throw new Error('gallery-real: no gallery page entry (PresentRealGalleryPage) found — retained vocabulary unverifiable');
  }
  let oraclePixels = 0;
  for (const sym of ORACLE_SYMBOLS) {
    if (body.includes(sym)) {
      oraclePixels = 1;
      break;
    }
  }
  if (!/renderer\.draw|EmitGalleryWindowCmds/.test(body)) {
    throw new Error('gallery-real: page entry emits no retained vocabulary cmds (no renderer.draw emission)');
  }
  return { kinds: RETAINED_VOCABULARY.slice(), oraclePixels };
}

// Default T3 gate run: drives the committed hermetic shell gate against
// `codeRoot` (no device, no network). Resolves `{ ok, symbol? }` where a red
// gate names the offending oracle symbol.
async function defaultRunGate({ codeRoot } = {}) {
  const { spawnSync } = require('node:child_process');
  const root = codeRoot || process.cwd();
  const gate = path.join(__dirname, '..', '..', '..', 'engine', 'runtime', 'tests', 'gallery_page_no_oracle_check.sh');
  let res;
  try {
    res = spawnSync('sh', [gate, root], { encoding: 'utf8' });
  } catch (err) {
    throw new Error(`gallery-real: T3 gate failed to run: ${(err && err.message) || String(err)}`);
  }
  const combined = `${(res && res.stdout) || ''}\n${(res && res.stderr) || ''}`;
  if (res && res.status === 0) return { ok: true };
  let symbol = null;
  const match = combined.match(/PaintCaption|PaintOracleBox|RasterGalleryPage/);
  if (match) symbol = match[0];
  return { ok: false, symbol };
}

// Default interactive probes: read the T2/T5 sources under `codeRoot` and
// report one boolean per owning-criteria probe (tap dispatch, state-layer
// wash, theme/Dynamic Color repaint, IME-inset sheet anchoring, layering
// overlay order). No device, no network.
async function defaultRunProbes({ codeRoot, fs: fsDouble } = {}) {
  const reader = fsDouble || fs;
  const root = codeRoot || process.cwd();
  const src = path.join(root, 'engine', 'runtime', 'src', 'gallery_present.cpp');
  let text;
  try {
    text = reader.readFileSync(src, 'utf8');
  } catch (err) {
    throw new Error(`gallery-real: cannot read gallery sources: ${src}: ${(err && err.message) || String(err)}`);
  }
  return {
    tap: /onScrimTap/.test(text),
    state: /StateLayerOpacity|PaintStateWash|BuildRetainedGalleryRowCmd/.test(text),
    themeRepaint: /theme/i.test(text),
    imeInset: /anchorSheetToImeInset|SetPresentImeBottom|imeBottom/.test(text),
    layering: /paintLayeringPage|LayeringScene|OverlayScrim/.test(text),
  };
}

// Run the retained-component or interactive gate. `deps` is the hermetic
// test seam `{ readT2Emission, runGate, runProbes }` (each
// `async (opts) => result`); the production defaults read the T2/T3 sources
// under `codeRoot`. `fs` is an optional filesystem double. Resolves
// `{ ok, message }` carrying the exact CLI ok marker; rejects naming the
// failing probe or symbol (the CLI maps these to exit 1, never a silent
// pass). Never throws across misuse without the Usage line.
async function runGalleryRealT4(opts = {}) {
  const codeRoot = opts.codeRoot || process.cwd();
  const deps = opts.deps || {};
  const fsDouble = opts.fs || undefined;
  if (opts.assertRetainedComponents && opts.assertInteractive) {
    throw new Error(
      `Error: --assert-retained-components and --assert-interactive are mutually exclusive (choose one)\n${USAGE}`,
    );
  }
  if (opts.assertRetainedComponents) {
    const readT2Emission = deps.readT2Emission || defaultReadT2Emission;
    const runGate = deps.runGate || defaultRunGate;
    const emission = await readT2Emission({ codeRoot, fs: fsDouble });
    const kinds = (emission && emission.kinds) || [];
    for (const kind of kinds) {
      if (!RETAINED_VOCABULARY.includes(kind)) {
        throw new Error(
          `gallery-real: non-retained kind '${kind}' emitted (expected retained vocabulary: ${RETAINED_VOCABULARY.join(', ')})`,
        );
      }
    }
    if (emission && Number(emission.oraclePixels) > 0) {
      throw new Error(
        `gallery-real: oracle pixels produced on the real-component page (AC-GRAPHICS-042: zero oracle output)`,
      );
    }
    const gate = await runGate({ codeRoot });
    if (!gate || gate.ok !== true) {
      const symbol = (gate && gate.symbol) || 'oracle painter';
      throw new Error(`gallery-real: T3 gate red — oracle painter reachable from gallery page target: ${symbol}`);
    }
    const message = 'gallery-real ok: retained-components';
    return { ok: message, message };
  }
  if (opts.assertInteractive) {
    const runProbes = deps.runProbes || defaultRunProbes;
    const probes = await runProbes({ codeRoot, fs: fsDouble });
    const order = [
      ['tap', 'tap dispatch'],
      ['state', 'state-layer wash'],
      ['themeRepaint', 'theme repaint'],
      ['imeInset', 'IME inset'],
      ['layering', 'layering order'],
    ];
    for (const [key, label] of order) {
      if (!probes || probes[key] !== true) {
        throw new Error(`gallery-real: interactive probe failed: ${label} (${key})`);
      }
    }
    const message = 'gallery-real ok: interactive';
    return { ok: message, message };
  }
  throw new Error(`Error: missing required flag --assert-retained-components|--assert-interactive\n${USAGE}`);
}

// @muse/cli — `muse-native gallery-real --assert-no-diagram [--page <target> ...]`
// TC-M3-026 gate (FR-M3-011 / AC-M3-025): enforces mock-painter
// unreachability as a build failure. A fixture page importing a mock-painter
// symbol fails the build with the symbol named; the TC-M3-026 check passes on
// a clean tree. Oracle diagram painters remain importable only from
// unit-test/hermetic targets — never from src/ page targets or device frames.
// Thin wrapper mirroring gallery-proof.js / gallery-layering.js structure
// (arg parser, usage on misuse, callable entry point).

const M3_USAGE = 'Usage: muse-native gallery-real --assert-no-diagram [--assert-counts] [--assert-interactive] [--assert-real-pixels] [--page <target> ...]';

// Mock-painter symbol pattern (RULE-M3-007, RULE-GRAPHICS-003): the oracle
// diagram painters (`MockPainter`, the `PaintCaption` box/caption stand-in,
// any other `mock` diagram painter) must never be reachable from a gallery
// page target on the device path.
const MOCK_PAINTER_RE = /MockPainter|PaintCaption|mock/i;

// Source-scan pattern for the on-disk page-target sweep: narrower than the
// pageTarget gate above by design. Bare `mock` would false-positive on the
// legitimate `mockReachable` BuildError plumbing in gallerySurface.js (T1);
// only concrete mock-painter symbols fail the build here. Case-insensitive
// (painter-symbol shapes, not case-sensitivity, carry the exemption).
const SOURCE_MOCK_PAINTER_RE = /mock\s*painter|paint\s*caption/i;

const MAX_PAGE_TARGET_LENGTH = 1024;

// Prototype-pollution / fail-closed denylist: these values never name a real
// gallery page target, so they throw instead of passing silently.
const UNSAFE_TARGETS = new Set(['__proto__', 'prototype', 'constructor']);

class MockReachableError extends Error {
  constructor(symbol, detail) {
    super(detail ? `mock-painter reachable: ${symbol}: ${detail}` : `mock-painter reachable: ${symbol}`);
    this.name = 'MockReachableError';
    this.symbol = symbol;
    this.code = 'mock-reachable';
  }
}

function failUnreachable(symbol, detail) {
  throw new MockReachableError(symbol, detail);
}

// `assertNoMockPainter(pageTarget: string): void throws MockReachableError —
// pageTarget is a gallery page target; throws MockReachableError{symbol}
// when any mock-painter symbol is reachable`.
//
// Fail-closed: a non-string, empty/blank, oversized, or otherwise unsafe
// pageTarget throws (never passes silently); a target reaching a
// mock-painter symbol throws naming that symbol; a clean page target
// returns without throwing.
function assertNoMockPainter(pageTarget) {
  if (typeof pageTarget !== 'string' || pageTarget.length === 0) {
    failUnreachable('invalid-pageTarget', 'pageTarget must be a non-empty string');
  }
  if (pageTarget.length > MAX_PAGE_TARGET_LENGTH || /^\s*$/.test(pageTarget)) {
    failUnreachable('invalid-pageTarget', 'pageTarget is blank or oversized');
  }
  if (UNSAFE_TARGETS.has(pageTarget) || /[\0-\x1f\x7f]/.test(pageTarget) || /(^|\/)\.\.(\/|$)/.test(pageTarget)) {
    failUnreachable(pageTarget.slice(0, 64), 'unsafe pageTarget is never a clean page');
  }
  const hit = MOCK_PAINTER_RE.exec(pageTarget);
  if (hit) {
    failUnreachable(hit[0], `page target reaches mock-painter symbol (${pageTarget.slice(0, 128)})`);
  }
  return undefined;
}

// T3 (FR-M3-011 / AC-M3-026): in-place interactivity report per gallery
// page kind, through the engine-owned Surface host contract. Probe order per
// contract A1: this module owns the `--assert-interactive` gate (next to
// `assertNoMockPainter`); Showcase.tsx renders the real components, and
// galleryNav.js keeps shell-chrome-only paging (no tap-to-advance).
//
// Per-kind behavior (contract A2/A5 — which kinds are live, not magnitudes):
// - `controls`: enabled selection controls — tap live.
// - `disabled-controls`: disabled controls — every kind inert.
// - `text`: text field — focus + caret/selection live.
// - `slider`: scrub page — press-and-drag never fires the tap handler.
// - `dialog`: dialog/sheet/menu — scrim dismissal live.
// - `scroll-list`: scrollable page — scroll live in place (never pages).
const INTERACTIVE_REPORTS = {
  controls: { tap: true, focus: false, caret: false, scroll: false, dismiss: false },
  text: { tap: false, focus: true, caret: true, scroll: false, dismiss: false },
  slider: { tap: false, focus: false, caret: false, scroll: false, dismiss: false },
  dialog: { tap: false, focus: false, caret: false, scroll: false, dismiss: true },
  'disabled-controls': { tap: false, focus: false, caret: false, scroll: false, dismiss: false },
  'scroll-list': { tap: false, focus: false, caret: false, scroll: true, dismiss: false },
};

const MAX_PAGE_ID_LENGTH = 1024;

function unknownPageError(pageId) {
  const shown = typeof pageId === 'string' ? pageId.slice(0, 128) : String(pageId).slice(0, 128);
  const err = new Error(`unknown gallery page: ${shown}`);
  err.code = 'unknown-page';
  return err;
}

// `runInteractiveAssertions(pageId: string): InteractiveReport — pageId
// names a gallery page; returns
// InteractiveReport{tap, focus, caret, scroll, dismiss}`.
//
// Fail-closed (contract A3/A4): unknown, empty, blank, non-string, or
// oversized pageIds throw an Error with `code === 'unknown-page'` — never a
// silent all-false report. Each call returns a fresh object.
function runInteractiveAssertions(pageId) {
  if (typeof pageId !== 'string' || pageId.length === 0 || /^\s*$/.test(pageId)) {
    throw unknownPageError(pageId);
  }
  if (pageId.length > MAX_PAGE_ID_LENGTH) {
    throw unknownPageError(pageId);
  }
  if (/[\0-\x1f\x7f]/.test(pageId)) {
    throw unknownPageError(pageId);
  }
  if (UNSAFE_TARGETS.has(pageId)) {
    throw unknownPageError(pageId);
  }
  if (!Object.prototype.hasOwnProperty.call(INTERACTIVE_REPORTS, pageId)) {
    throw unknownPageError(pageId);
  }
  const report = INTERACTIVE_REPORTS[pageId];
  if (!report) {
    throw unknownPageError(pageId);
  }
  return { tap: report.tap, focus: report.focus, caret: report.caret, scroll: report.scroll, dismiss: report.dismiss };
}

// Parse `muse-native gallery-real --assert-no-diagram [--page <target> ...]`.
// Follows the gallery-proof CLI pattern (`parseGalleryProofArgs` /
// `runGalleryTree(argv, env)`): known flags only, a missing `--page` value
// throws, unknown flags throw.
function parseGalleryRealM3Args(argv, env) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-real') args.shift();
  const injected = env !== undefined ? env : process.env;
  const envObj = injected && typeof injected === 'object' ? injected : {};
  void envObj;
  const known = new Set(['--assert-no-diagram', '--assert-counts', '--assert-interactive', '--assert-real-pixels', '--page', '-h', '--help']);
  const parsed = { assertNoDiagram: false, assertCounts: false, assertInteractive: false, assertRealPixels: false, pages: [], help: false };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (!known.has(arg)) {
      const err = new Error(`Error: unknown option '${String(arg).slice(0, 64)}'. Expected one of: --assert-no-diagram, --assert-counts, --assert-interactive, --assert-real-pixels, --page <target>`);
      err.code = 'GALLERY_REAL_USAGE';
      throw err;
    }
    if (arg === '--assert-no-diagram') parsed.assertNoDiagram = true;
    else if (arg === '--assert-counts') parsed.assertCounts = true;
    else if (arg === '--assert-interactive') parsed.assertInteractive = true;
    else if (arg === '--assert-real-pixels') parsed.assertRealPixels = true;
    else if (arg === '-h' || arg === '--help') parsed.help = true;
    else if (arg === '--page') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --page requires a value. Expected a gallery page target');
        err.code = 'GALLERY_REAL_USAGE';
        throw err;
      }
      parsed.pages.push(next);
      i++;
    }
  }
  return parsed;
}

// Reachability scan of the gallery src/ page targets on disk: every
// device-frame source file must be free of mock-painter symbols. Returns the
// hits (`[]` on a clean tree); the caller fails the build naming the first
// symbol. Unit-test/hermetic targets (test/, tests/, fixtures) are out of
// scope by design — oracle diagram painters stay importable only from there.
function scanPageSources(codeRoot) {
  const gallerySrc = path.join(codeRoot, 'examples', 'm3-gallery', 'src');
  let entries = [];
  try {
    entries = fs.readdirSync(gallerySrc, { withFileTypes: true });
  } catch (_) {
    return [];
  }
  const hits = [];
  for (const entry of entries) {
    if (!entry.isFile() || !/\.(tsx|ts|js|jsx)$/.test(entry.name)) continue;
    let src = null;
    try {
      src = fs.readFileSync(path.join(gallerySrc, entry.name), 'utf8');
    } catch (_) {
      continue;
    }
    const m = SOURCE_MOCK_PAINTER_RE.exec(src);
    if (m) hits.push({ file: path.join('examples', 'm3-gallery', 'src', entry.name), symbol: m[0] });
  }
  return hits;
}

function runGalleryRealM3(argv, env) {
  let opts = null;
  try {
    opts = parseGalleryRealM3Args(argv, env);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\n${M3_USAGE}\n`);
    process.exitCode = 2;
    return undefined;
  }
  if (opts.help) {
    process.stdout.write(`${M3_USAGE}\n`);
    process.exitCode = 2;
    return undefined;
  }
  // Fail closed: asserting nothing is a usage error, never a silent pass.
  if (!opts.assertNoDiagram && !opts.assertCounts && !opts.assertInteractive && !opts.assertRealPixels) {
    process.stderr.write(`Error: nothing to assert. Pass --assert-no-diagram.\n${M3_USAGE}\n`);
    process.exitCode = 2;
    return undefined;
  }
  try {
    for (const page of opts.pages) {
      assertNoMockPainter(page);
    }
    if (opts.pages.length === 0) {
      const hits = scanPageSources(process.cwd());
      if (hits.length > 0) {
        throw new MockReachableError(hits[0].symbol, `${hits[0].file} reaches a mock-painter symbol`);
      }
    }
    if (opts.assertRealPixels) {
      // T5 (FR-M3-011 / AC-M3-027, TC-M3-028): real-pixel proof gate next to
      // --assert-no-diagram / --assert-interactive. Re-runs the
      // theme/Dynamic Color, IME-inset, error-state, and layering proof on
      // real-component pages in light and dark via runRealPixelProof; golden
      // re-baseline stays blocked while blankViewports/goldenDiffs are red.
      let proof = null;
      try {
        proof = require('../../../examples/m3-gallery/src/gallery-proof.js');
      } catch (_) {
        proof = null;
      }
      if (!proof || typeof proof.runRealPixelProof !== 'function') {
        const err = new Error('gallery-real failed: real-pixel proof unavailable');
        process.stderr.write(`${err.message}\n`);
        process.exitCode = 1;
        return undefined;
      }
      for (const theme of ['light', 'dark']) {
        const report = proof.runRealPixelProof(theme);
        if (report.blankViewports !== 0 || report.goldenDiffs !== 0) {
          process.stderr.write(
            `gallery-real failed: real pixels red for theme ${theme} ` +
            `(blankViewports=${report.blankViewports} goldenDiffs=${report.goldenDiffs}); re-baseline blocked\n`,
          );
          process.exitCode = 1;
          return undefined;
        }
      }
    }
    if (opts.assertInteractive) {
      const targets = opts.pages.length === 0 ? Object.keys(INTERACTIVE_REPORTS) : opts.pages;
      for (const page of targets) {
        runInteractiveAssertions(page);
      }
    }
  } catch (err) {
    if (err && err.code === 'GALLERY_REAL_USAGE') {
      process.stderr.write(`${err.message}\n${M3_USAGE}\n`);
      process.exitCode = 2;
      return undefined;
    }
    const symbol = (err && typeof err.symbol === 'string' && err.symbol) || 'mock-painter';
    process.stderr.write(`gallery-real failed: mock-painter reachable: ${symbol}\n`);
    process.exitCode = 1;
    return undefined;
  }
  const count = opts.pages.length === 0 ? 'tree' : String(opts.pages.length);
  process.stdout.write(`gallery-real ok: no mock painter reachable (${count})\n`);
  process.exitCode = 0;
  return { pass: true };
}

// ---- merged dispatch (resolve-conflicts round 2) ----
// Canonical `parseGalleryRealArgs` / `runGalleryReal` route by input shape so
// one module serves both legs:
//   * FR-GRAPHICS-015 T4 (default): `--assert-retained-components` /
//     `--assert-interactive [--code-root <dir>]` source-gate semantics.
//   * Upstream FR-M3-011: `--assert-no-diagram` / `--assert-counts` /
//     `--assert-real-pixels` / `--page` (argv form) and the
//     `assertNoMockPainter` / `runInteractiveAssertions` report surface.
// `--assert-interactive` alone parses under T4 (same boolean); the CLI entry
// (`bin/muse-native.js`) routes it to the device-interactive leg unless a
// T4 companion flag (`--code-root`) or `--assert-retained-components` is
// present. Bare/unknown-flag misuse always throws T4 usage (fail-closed).
const M3_ONLY_FLAGS = new Set([
  '--assert-no-diagram',
  '--assert-counts',
  '--assert-real-pixels',
  '--page',
]);
const T4_ONLY_FLAGS = new Set(['--assert-retained-components', '--code-root']);

function parseGalleryRealArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-real') args.shift();
  const hasT4 = args.some((a) => T4_ONLY_FLAGS.has(a));
  const hasM3 = args.some((a) => M3_ONLY_FLAGS.has(a));
  if (hasM3 && !hasT4) return parseGalleryRealM3Args(argv);
  return parseGalleryRealT4Args(argv);
}

function runGalleryReal(opts, env) {
  if (Array.isArray(opts)) return runGalleryRealM3(opts, env);
  return runGalleryRealT4(opts);
}

module.exports = {
  parseGalleryRealArgs,
  runGalleryReal,
  parseGalleryRealT4Args,
  runGalleryRealT4,
  parseGalleryRealM3Args,
  runGalleryRealM3,
  assertNoMockPainter,
  runInteractiveAssertions,
  MockReachableError,
  MOCK_PAINTER_RE,
  RETAINED_VOCABULARY,
  USAGE,
  M3_USAGE,
};

if (require.main === module) {
  runGalleryReal(process.argv.slice(2));
}
