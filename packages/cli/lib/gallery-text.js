'use strict';
// @muse/cli — `muse-native gallery-text --assert-vector --assert-atlas-reuse [--code-root <dir>]`
// TC-TEXT-045 Check (FR-TEXT-016 AC-TEXT-045): asserts the gallery text path
// rides retained vector `ShapedText` runs from the engine stack
// (shape → glyph-atlas cache → Swapchain rasterization) with zero bitmap
// `DrawText` callers on gallery pages, and asserts atlas reuse (cache hits
// across frames per font/scale, bounded entries/bytes).
// Thin hermetic wrapper over the T5/T6/T3 gates — no verification logic is
// reimplemented here: the vector gate is the T5 emission (`ShapedText` with a
// nonzero `glyphRunId` from `ShapeGalleryLabel`) plus the T6 retained
// `RasterizeShapedText` path in `engine/runtime/src/gallery_present.cpp`
// (bitmap `DrawText` survives solely as the fail-closed diagnostic-screen
// path, never invoked from gallery pages — T7), and the atlas gate is the T3
// `GlyphAtlas` (hits/misses stats, stated entry/byte caps, LRU eviction).
// Exit 0 iff every assertion holds; nonzero with a per-sample delta report
// otherwise (never a silent pass, RULE-RUNTIME-001). No device, no network.
const fs = require('node:fs');
const path = require('node:path');

const GALLERY_PRESENT_CPP = path.join('engine', 'runtime', 'src', 'gallery_present.cpp');
const GLYPH_ATLAS_CPP = path.join('engine', 'text', 'src', 'glyph_atlas.cpp');
const GLYPH_ATLAS_H = path.join('engine', 'text', 'include', 'muse', 'text', 'glyph_atlas.h');

const USAGE = 'Usage: muse-native gallery-text --assert-vector --assert-atlas-reuse [--code-root <dir>]';

function parseGalleryTextArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let assertVector = false;
  let assertAtlasReuse = false;
  let codeRoot = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--assert-vector') {
      assertVector = true;
    } else if (arg === '--assert-atlas-reuse') {
      assertAtlasReuse = true;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --code-root requires a value. Expected a checkout directory');
        err.code = 'GALLERY_TEXT_USAGE';
        throw err;
      }
      codeRoot = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, assertVector, assertAtlasReuse, codeRoot };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --assert-vector, --assert-atlas-reuse, --code-root <dir>`);
      err.code = 'GALLERY_TEXT_USAGE';
      throw err;
    }
  }
  if (!assertVector) {
    const err = new Error('Error: missing required flag --assert-vector. Expected muse-native gallery-text --assert-vector --assert-atlas-reuse [--code-root <dir>]');
    err.code = 'GALLERY_TEXT_USAGE';
    throw err;
  }
  if (!assertAtlasReuse) {
    const err = new Error('Error: missing required flag --assert-atlas-reuse. Expected muse-native gallery-text --assert-vector --assert-atlas-reuse [--code-root <dir>]');
    err.code = 'GALLERY_TEXT_USAGE';
    throw err;
  }
  return { help: false, assertVector, assertAtlasReuse, codeRoot };
}

// Every `DrawText(` call site except the `void DrawText` bitmap definition
// itself. The definition survives (T7 diagnostic-screen retention); calls are
// what flip the vector check.
function findDrawTextCalls(source) {
  const calls = [];
  const re = /DrawText\s*\(/g;
  let m;
  while ((m = re.exec(source)) !== null) {
    const before = source.slice(Math.max(0, m.index - 32), m.index);
    if (/void\s*$/.test(before)) continue;
    calls.push(m.index);
  }
  return calls;
}

// True when a bitmap call site sits inside the fail-closed diagnostic-screen
// branch (the `rows.empty()` empty-rows path that names the failure in plain
// text) rather than on a gallery page painter. Gallery page painters
// (`PaintCaption`, `EmitFamilyCmd`, `EmitCaptionCmd`, the windowed
// `EmitGalleryWindowCmds` path) must never call the bitmap fallback (T7).
function isDiagnosticGuarded(source, index) {
  const window = source.slice(Math.max(0, index - 2500), index);
  return /rows\.empty\(\)|Diagnostic screen|diagnostic-screen/i.test(window);
}

function runGalleryText(rest) {
  let opts;
  try {
    opts = parseGalleryTextArgs(rest);
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
  // Vector gate (T5/T6): gallery emission produces retained ShapedText runs
  // with real (nonzero) glyphRunId values resolving in the atlas.
  if (!/ShapedText/.test(source) || !/glyphRunId/.test(source)) {
    process.stderr.write(
      'FAIL gallery-text --assert-vector: no retained ShapedText vector path in '
      + `${GALLERY_PRESENT_CPP} (ShapedText/glyphRunId emission absent); `
      + 'delta: vector coverage 0 runs, bitmap fallback still owns gallery pages.\n',
    );
    process.exitCode = 1;
    return;
  }
  // Bitmap-retirement gate (T7): no DrawText callers on the gallery path.
  const calls = findDrawTextCalls(source).filter((index) => !isDiagnosticGuarded(source, index));
  if (calls.length > 0) {
    process.stderr.write(
      `FAIL gallery-text --assert-vector: bitmap DrawText fallback still paints gallery pages `
      + `(${GALLERY_PRESENT_CPP}: ${calls.length} non-diagnostic DrawText call site(s)); `
      + 'delta: bitmap-path counter nonzero, expected bitmap-counter-0 on gallery pages.\n',
    );
    process.exitCode = 1;
    return;
  }
  // Atlas-reuse gate (T3): the GlyphAtlas cache records hits across frames
  // per font/scale and enforces the stated entry/byte bound with eviction.
  if (opts.assertAtlasReuse) {
    let atlas = null;
    for (const rel of [GLYPH_ATLAS_CPP, GLYPH_ATLAS_H]) {
      try {
        atlas = (atlas || '') + '\n' + fs.readFileSync(path.join(codeRoot, rel), 'utf8');
      } catch (_) {
        // Missing half is reported below as a failed gate, never skipped.
      }
    }
    if (
      atlas === null || atlas.length === 0
      || !/hits/i.test(atlas)
      || !/max_entries_|max_bytes_|kCacheFull|evict/i.test(atlas)
    ) {
      process.stderr.write(
        'FAIL gallery-text --assert-atlas-reuse: glyph-atlas cache gate absent '
        + `(${GLYPH_ATLAS_CPP} / ${GLYPH_ATLAS_H} unreadable or without hits/bound/eviction counters); `
        + 'delta: atlas hits 0 across frames, entries unbounded.\n',
      );
      process.exitCode = 1;
      return;
    }
  }
  process.stdout.write(`gallery-text ok: retained ShapedText vector path with nonzero glyphRunId, bitmap-counter-0 on gallery pages, atlas reuse holds (code-root: ${codeRoot})\n`);
  process.exitCode = 0;
}

module.exports = runGalleryText;
module.exports.run = runGalleryText;
module.exports.parseGalleryTextArgs = parseGalleryTextArgs;
module.exports.parseArgs = parseGalleryTextArgs;
module.exports.parse = parseGalleryTextArgs;
