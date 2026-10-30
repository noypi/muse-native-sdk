'use strict';
// @muse/cli — `muse-native renderer-prims --assert-m3-set --assert-fail-closed [--code-root <dir>]`
// TC-GRAPHICS-029 Check (FR-GRAPHICS-010 AC-GRAPHICS-029): asserts the
// retained 11-kind `PrimitiveKind` vocabulary holds and undeclared kinds
// fail closed (RULE-RUNTIME-001).
// Thin wrapper over the existing hermetic `render_smoke_test` binary /
// assertions: the source-of-truth check parses the `PrimitiveKind` enum in
// `engine/runtime/include/runtime/renderer.h` under `--code-root`, and when
// a built hermetic `render_smoke_test` binary is present under that root it
// is executed as the assertion reuse (a nonzero binary result fails the
// check). No device, no network, no downloads — hermetic only.
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

// Retained M3 primitive vocabulary (FR-GRAPHICS-010 AC-GRAPHICS-029).
const ELEVEN_KINDS = [
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

const RENDERER_H = path.join('engine', 'runtime', 'include', 'runtime', 'renderer.h');

// Candidate locations of the hermetic render_smoke_test binary relative to
// a code root (host build first, then per-ABI builds).
const SMOKE_BINARIES = [
  path.join('build', 'host', 'render_smoke_test'),
  path.join('build', 'android-x86_64', 'render_smoke_test'),
  path.join('build', 'android-x86', 'render_smoke_test'),
  path.join('build', 'android-arm64-v8a', 'render_smoke_test'),
  path.join('build', 'android-armeabi-v7a', 'render_smoke_test'),
];

function parseRendererPrimsArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  let assertM3Set = false;
  let assertFailClosed = false;
  let codeRoot = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--assert-m3-set') {
      assertM3Set = true;
    } else if (arg === '--assert-fail-closed') {
      assertFailClosed = true;
    } else if (arg === '--code-root') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        const err = new Error('Error: --code-root requires a value. Expected a checkout directory');
        err.code = 'RENDERER_PRIMS_USAGE';
        throw err;
      }
      codeRoot = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      return { help: true, assertM3Set, assertFailClosed, codeRoot };
    } else {
      const err = new Error(`Error: unknown option '${arg}'. Expected one of: --assert-m3-set, --assert-fail-closed, --code-root <dir>`);
      err.code = 'RENDERER_PRIMS_USAGE';
      throw err;
    }
  }
  return { help: false, assertM3Set, assertFailClosed, codeRoot };
}

// Extract the enumerator names from the `enum class PrimitiveKind { ... }`
// declaration in renderer.h source text.
function parsePrimitiveKinds(source) {
  const match = /enum\s+class\s+PrimitiveKind\s*\{([^}]*)\}/.exec(source);
  if (!match) return null;
  return match[1]
    .split(',')
    .map((part) => part.trim().split(/\s*=\s*/)[0].trim())
    .filter((name) => /^[A-Za-z_][A-Za-z0-9_]*$/.test(name));
}

function findSmokeBinary(codeRoot) {
  for (const rel of SMOKE_BINARIES) {
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

function runRendererPrims(rest) {
  let opts;
  try {
    opts = parseRendererPrimsArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native renderer-prims --assert-m3-set --assert-fail-closed [--code-root <dir>]\n`);
    process.exitCode = 2;
    return;
  }
  if (opts.help) {
    process.stdout.write('Usage: muse-native renderer-prims --assert-m3-set --assert-fail-closed [--code-root <dir>]\n');
    process.exitCode = 0;
    return;
  }
  // Fail closed on bad invocation: both assert flags are required, never a
  // silent pass (RULE-RUNTIME-001).
  if (!opts.assertM3Set || !opts.assertFailClosed) {
    const missing = !opts.assertM3Set ? '--assert-m3-set' : '--assert-fail-closed';
    process.stderr.write(`Error: missing required flag ${missing}. Expected muse-native renderer-prims --assert-m3-set --assert-fail-closed [--code-root <dir>]\n`);
    process.exitCode = 2;
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
    process.stderr.write(`Error: --code-root '${opts.codeRoot}' does not exist or is not a directory.\n`);
    process.exitCode = 1;
    return;
  }
  let source = null;
  try {
    source = fs.readFileSync(path.join(codeRoot, RENDERER_H), 'utf8');
  } catch (_) {
    source = null;
  }
  if (source === null) {
    process.stderr.write(`Error: cannot read ${RENDERER_H} under code root '${codeRoot}'.\n`);
    process.exitCode = 1;
    return;
  }
  const kinds = parsePrimitiveKinds(source);
  if (kinds === null) {
    process.stderr.write(`Error: no 'enum class PrimitiveKind' declaration found in ${RENDERER_H}.\n`);
    process.exitCode = 1;
    return;
  }
  const seen = new Set(kinds);
  const missing = ELEVEN_KINDS.filter((kind) => !seen.has(kind));
  const extra = kinds.filter((kind) => !ELEVEN_KINDS.includes(kind));
  if (missing.length > 0) {
    process.stderr.write(`Error: retained 11-kind M3 set broken — missing: ${missing.join(', ')}.\n`);
    process.exitCode = 1;
    return;
  }
  if (extra.length > 0) {
    // An undeclared kind that does not fail closed breaks RULE-RUNTIME-001.
    process.stderr.write(`Error: fail-closed broken — undeclared kinds present: ${extra.join(', ')}.\n`);
    process.exitCode = 1;
    return;
  }
  // Reuse the hermetic render_smoke_test binary/assertions when a build is
  // present under the code root. Absence is not a failure (the enum check
  // above is authoritative for fixture roots); a nonzero run is.
  const smoke = findSmokeBinary(codeRoot);
  if (smoke !== null) {
    const res = spawnSync(smoke, [], { encoding: 'utf8' });
    if (res.error || res.status !== 0) {
      const detail = res.error ? String(res.error.message || res.error) : String(res.stderr || '').slice(0, 500);
      process.stderr.write(`Error: hermetic render_smoke_test failed (${smoke}): ${detail}\n`);
      process.exitCode = 1;
      return;
    }
  }
  process.stdout.write(`renderer-prims ok: 11-kind M3 set holds, fail-closed holds (code-root: ${codeRoot})\n`);
  process.exitCode = 0;
}

module.exports = runRendererPrims;
module.exports.run = runRendererPrims;
module.exports.parseRendererPrimsArgs = parseRendererPrimsArgs;
module.exports.ELEVEN_KINDS = ELEVEN_KINDS;
