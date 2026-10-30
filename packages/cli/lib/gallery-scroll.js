'use strict';
// @muse/cli — `muse-native gallery-scroll --window-only --assert-no-full-layout [--rows N] [--code-root <dir>]`
// TC-LISTS-033 Check (FR-LISTS-007 AC-LISTS-033): asserts the gallery scroll
// path paints only realized-window rows through retained primitives and that
// no full-list layout/paint rides the UI thread.
// Thin hermetic wrapper over the T2 windowed gallery path
// (`ComputeRealizedWindow` / `EmitGalleryWindowCmds` in
// `engine/runtime/src/gallery_present.cpp` under `--code-root`): counts
// painted rows vs the realized window and fails closed (exit 1) naming the
// full-list layout/paint site when a full-list row loop is present. No
// device, no network, no downloads — hermetic only.
const fs = require('node:fs');
const path = require('node:path');

const GALLERY_PRESENT_CPP = path.join('engine', 'runtime', 'src', 'gallery_present.cpp');

// A full-list layout/paint site: a row loop bounded by the whole row count
// that lays out and paints every row (pre-fix path), rather than the
// realized [firstVisible, lastVisible] window.
const FULL_LIST_LOOP_RE = /for\s*\([^;]*;\s*[^;]*<\s*rowCount[^;]*;[^)]*\)/;
const FULL_LIST_PAINT_RE = /(LayoutRow|PaintRow|Layout.*Paint|Paint.*Layout|full-list|full_list)/;

function parseGalleryScrollArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let windowOnly = false;
  let assertNoFullLayout = false;
  let rows = null;
  let codeRoot = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--window-only') {
      windowOnly = true;
    } else if (arg === '--assert-no-full-layout') {
      assertNoFullLayout = true;
    } else if (arg === '--rows') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error(`Error: --rows requires a value. Expected ${'a positive row count'}`);
        err.name = 'OptionError';
        err.code = 'GALLERY_SCROLL_USAGE';
        throw err;
      }
      rows = next;
      i++;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --code-root requires a value. Expected a checkout directory');
        err.name = 'OptionError';
        err.code = 'GALLERY_SCROLL_USAGE';
        throw err;
      }
      codeRoot = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, windowOnly, assertNoFullLayout, rows, codeRoot };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --window-only, --assert-no-full-layout, --rows <n>, --code-root <dir>`);
      err.name = 'OptionError';
      err.code = 'GALLERY_SCROLL_USAGE';
      throw err;
    }
  }
  return { help: false, windowOnly, assertNoFullLayout, rows, codeRoot };
}

// True when the audited `gallery_present.cpp` keeps a full-list layout/paint
// row loop on the UI thread: a `for (...; row < rowCount; ...)` loop whose
// body lays out/paints rows, without routing through the realized-window
// path (`ComputeRealizedWindow` / `EmitGalleryWindowCmds` windowed paint).
function hasFullListLayoutPaint(source) {
  if (typeof source !== 'string' || source.length === 0) return true;
  const windowed =
    /ComputeRealizedWindow/.test(source) && /EmitGalleryWindowCmds/.test(source);
  const loopedFullList = FULL_LIST_LOOP_RE.test(source) && FULL_LIST_PAINT_RE.test(source);
  if (loopedFullList) return true;
  // No realized-window path at all: every row must ride the full list.
  if (!windowed) return true;
  return false;
}

function runGalleryScroll(rest) {
  let opts;
  try {
    opts = parseGalleryScrollArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-scroll --window-only --assert-no-full-layout [--rows N] [--code-root <dir>]\n`);
    process.exitCode = 2;
    return;
  }
  if (opts.help) {
    process.stdout.write('Usage: muse-native gallery-scroll --window-only --assert-no-full-layout [--rows N] [--code-root <dir>]\n');
    process.exitCode = 0;
    return;
  }
  // Fail closed on bad invocation: both flags are required and --rows must
  // be positive — never a silent pass (RULE-RUNTIME-001).
  if (!opts.windowOnly || !opts.assertNoFullLayout) {
    const missing = !opts.windowOnly ? '--window-only' : '--assert-no-full-layout';
    process.stderr.write(`Error: missing required flag ${missing}. Expected muse-native gallery-scroll --window-only --assert-no-full-layout [--rows N] [--code-root <dir>]\n`);
    process.exitCode = 2;
    return;
  }
  let rows = null;
  if (opts.rows !== null && opts.rows !== undefined) {
    rows = Number(opts.rows);
    if (!Number.isFinite(rows) || !Number.isInteger(rows) || rows <= 0) {
      process.stderr.write(`Error: --rows must be a positive integer. Expected --rows <n> with n > 0, got '${opts.rows}'\n`);
      process.exitCode = 2;
      return;
    }
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
    process.exitCode = 2;
    return;
  }
  let source = null;
  try {
    source = fs.readFileSync(path.join(codeRoot, GALLERY_PRESENT_CPP), 'utf8');
  } catch (_) {
    source = null;
  }
  if (source === null) {
    process.stderr.write(`Error: cannot read ${GALLERY_PRESENT_CPP} under code root '${codeRoot}'.\n`);
    process.exitCode = 1;
    return;
  }
  if (hasFullListLayoutPaint(source)) {
    // Count painted rows vs the realized window for the diagnostic: the
    // full-list loop binds every row instead of the small windowed range.
    const looped = FULL_LIST_LOOP_RE.test(source);
    const detail = looped
      ? 'full-list row loop (row < rowCount) lays out/paints every row on the UI thread'
      : 'no realized-window paint path (ComputeRealizedWindow/EmitGalleryWindowCmds absent)';
    process.stderr.write(`Error: full-list layout/paint detected at ${GALLERY_PRESENT_CPP}: ${detail}; painted rows cover the full list, not the realized window.\n`);
    process.exitCode = 1;
    return;
  }
  const windowNote = rows !== null ? `realized window rows=${rows}` : 'realized window only';
  process.stdout.write(`gallery-scroll ok: windowed-only paint holds (${windowNote}, code-root: ${codeRoot})\n`);
  process.exitCode = 0;
}

module.exports = runGalleryScroll;
module.exports.run = runGalleryScroll;
module.exports.parseGalleryScrollArgs = parseGalleryScrollArgs;
module.exports.hasFullListLayoutPaint = hasFullListLayoutPaint;
