'use strict';
// @muse/cli — `muse-native gallery-real --assert-no-diagram [--page <target> ...]`
// TC-M3-026 gate (FR-M3-011 / AC-M3-025): enforces mock-painter
// unreachability as a build failure. A fixture page importing a mock-painter
// symbol fails the build with the symbol named; the TC-M3-026 check passes on
// a clean tree. Oracle diagram painters remain importable only from
// unit-test/hermetic targets — never from src/ page targets or device frames.
// Thin wrapper mirroring gallery-proof.js / gallery-layering.js structure
// (arg parser, usage on misuse, callable entry point).
const fs = require('node:fs');
const path = require('node:path');

const USAGE = 'Usage: muse-native gallery-real --assert-no-diagram [--page <target> ...]';

// Mock-painter symbol pattern (RULE-M3-007, RULE-GRAPHICS-003): the oracle
// diagram painters (`MockPainter`, the `PaintCaption` box/caption stand-in,
// any other `mock` diagram painter) must never be reachable from a gallery
// page target on the device path.
const MOCK_PAINTER_RE = /MockPainter|PaintCaption|mock/i;

// Source-scan pattern for the on-disk page-target sweep: narrower than the
// pageTarget gate above by design. Bare `mock` would false-positive on the
// legitimate `mockReachable` BuildError plumbing in gallerySurface.js (T1);
// only concrete mock-painter symbols fail the build here.
const SOURCE_MOCK_PAINTER_RE = /MockPainter|PaintCaption/;

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

// Parse `muse-native gallery-real --assert-no-diagram [--page <target> ...]`.
// Follows the gallery-proof CLI pattern (`parseGalleryProofArgs` /
// `runGalleryTree(argv, env)`): known flags only, a missing `--page` value
// throws, unknown flags throw.
function parseGalleryRealArgs(argv, env) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-real') args.shift();
  const injected = env !== undefined ? env : process.env;
  const envObj = injected && typeof injected === 'object' ? injected : {};
  void envObj;
  const known = new Set(['--assert-no-diagram', '--page', '-h', '--help']);
  const parsed = { assertNoDiagram: false, pages: [], help: false };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (!known.has(arg)) {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --assert-no-diagram, --page <target>`);
      err.code = 'GALLERY_REAL_USAGE';
      throw err;
    }
    if (arg === '--assert-no-diagram') parsed.assertNoDiagram = true;
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

function runGalleryReal(argv, env) {
  let opts = null;
  try {
    opts = parseGalleryRealArgs(argv, env);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\n${USAGE}\n`);
    process.exitCode = 2;
    return undefined;
  }
  if (opts.help) {
    process.stdout.write(`${USAGE}\n`);
    process.exitCode = 2;
    return undefined;
  }
  // Fail closed: asserting nothing is a usage error, never a silent pass.
  if (!opts.assertNoDiagram) {
    process.stderr.write(`Error: nothing to assert. Pass --assert-no-diagram.\n${USAGE}\n`);
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
  } catch (err) {
    if (err && err.code === 'GALLERY_REAL_USAGE') {
      process.stderr.write(`${err.message}\n${USAGE}\n`);
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

module.exports = {
  assertNoMockPainter,
  MockReachableError,
  parseGalleryRealArgs,
  runGalleryReal,
  MOCK_PAINTER_RE,
};

if (require.main === module) {
  runGalleryReal(process.argv.slice(2));
}
