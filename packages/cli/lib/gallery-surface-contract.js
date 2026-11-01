'use strict';
// gallery-surface-contract gate — FR-RUNTIME-011 (AC-RUNTIME-034, TC-RUNTIME-034).
//
// Drives the single Surface host seam sequence (attach/render/touch/theme/ime/
// detach/destroy) against the built gallery, then asserts frame stats show
// non-zero presents, logcat is FATAL-free, no fallback-to-static-root
// occurred, and the seam interface carries zero gallery-concept tokens.
// Every seam violation fails closed with `{ code, detail }`.

const fs = require('node:fs');
const path = require('node:path');

const REQUIRED_STEPS = ['attach', 'render', 'touch', 'theme', 'ime', 'detach', 'destroy'];

// Gallery-concept tokens forbidden in the seam interface (RULE-RUNTIME-007).
// Mirrors the `grep -rEn "page|stor|Prev|Next" presenter.h` CI leg.
const INTERFACE_TOKEN_RE = /page|stor|Prev|Next/;

const USAGE = 'Usage: muse-native gallery-real --assert-surface-contract [--device <serial>]';

// Parse `muse-native gallery-real --assert-surface-contract [--device <serial>]`.
// The leading `gallery-real` token is optional. Throws on a missing
// `--assert-surface-contract` flag, unknown flags, or a missing `--device`
// value (the CLI maps these to exit 2 with usage, never a silent pass).
function parseSurfaceContractArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-real') args.shift();
  let assertSurfaceContract = false;
  let device = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--assert-surface-contract') {
      assertSurfaceContract = true;
    } else if (arg === '--device') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw new Error(`Error: --device: Expected a value <serial>\n${USAGE}`);
      }
      device = next;
      i++;
    } else {
      throw new Error(`Error: unknown flag ${arg}\n${USAGE}`);
    }
  }
  if (!assertSurfaceContract) {
    throw new Error(`Error: missing required flag --assert-surface-contract\n${USAGE}`);
  }
  return { assertSurfaceContract: true, device };
}

function toFailure(code, detail) {
  const err = new Error(`SURFACE-CONTRACT-FAIL: ${code}: ${detail}`);
  err.code = code;
  err.detail = detail;
  return err;
}

function defaultEmit(line) {
  process.stdout.write(`${line}\n`);
}

// Default seam-interface reader: loads the real
// `engine/runtime/include/runtime/presenter.h` text. Injectable via
// `deps.readInterfaceText` so unit tests can supply header text without
// filesystem reads (same seam shape as `driveSequence` / `readLogcat`).
function defaultReadInterfaceText() {
  const header = path.resolve(
    __dirname, '..', '..', '..', 'engine', 'runtime', 'include', 'runtime', 'presenter.h');
  return fs.readFileSync(header, 'utf8');
}

// Run the surface-contract gate. `deps` is the hermetic test seam
// `{ driveSequence, readFrameStats, readLogcat, readInterfaceText }`;
// production defaults fail closed when no device runner is injected.
// `emit` is an optional line sink for per-step PASS lines (defaults to stdout).
async function runSurfaceContract(opts = {}) {
  const deps = opts.deps || {};
  const emit = typeof opts.emit === 'function' ? opts.emit : defaultEmit;
  const driveSequence = deps.driveSequence;
  const readFrameStats = deps.readFrameStats;
  const readLogcat = deps.readLogcat;
  const readInterfaceText = typeof deps.readInterfaceText === 'function'
    ? deps.readInterfaceText
    : defaultReadInterfaceText;

  if (typeof driveSequence !== 'function'
    || typeof readFrameStats !== 'function'
    || typeof readLogcat !== 'function') {
    throw toFailure('NO-DEVICE-RUNNER', 'no device runner injected for gallery-real --assert-surface-contract');
  }

  let driven;
  try {
    driven = await driveSequence({ device: opts.device || null });
  } catch (err) {
    const code = (err && typeof err.code === 'string' && err.code.length > 0)
      ? err.code
      : 'SEAM-STEP-FAIL';
    const step = (err && typeof err.step === 'string' && err.step.length > 0) ? `${err.step}: ` : '';
    const detail = `${step}${(err && err.message) || String(err)}`;
    throw toFailure(code, detail);
  }

  const steps = driven && Array.isArray(driven.steps) ? driven.steps : [];
  if (driven && driven.fallbackToStaticRoot) {
    throw toFailure('STATIC-ROOT-FALLBACK', 'fallback-to-static-root reported during surface-contract sequence');
  }
  for (const step of REQUIRED_STEPS) {
    if (!steps.includes(step)) {
      throw toFailure('MISSING-SEAM-STEP', `missing seam step in driven sequence: ${step} (got: ${steps.join(', ') || 'none'})`);
    }
  }

  const stats = await readFrameStats({ device: opts.device || null });
  const presented = stats && typeof stats.presented === 'number' ? stats.presented : 0;
  if (!(presented > 0)) {
    throw toFailure('ZERO-PRESENTS', `frame stats show zero presents (presented=${presented})`);
  }

  const logcat = await readLogcat({ device: opts.device || null });
  if (typeof logcat === 'string' && /fatal/i.test(logcat)) {
    throw toFailure('FATAL-LOGCAT', `FATAL found in logcat: ${logcat.slice(0, 200)}`);
  }

  // Zero gallery-concept tokens in the seam interface (RULE-RUNTIME-007,
  // plan Interface line). Fails closed with a named code on any match.
  let interfaceText;
  try {
    interfaceText = await readInterfaceText();
  } catch (err) {
    throw toFailure('GALLERY-TOKENS-IN-INTERFACE',
      `seam interface text unreadable: ${(err && err.message) || String(err)}`);
  }
  const tokenMatch = typeof interfaceText === 'string'
    ? interfaceText.match(INTERFACE_TOKEN_RE)
    : null;
  if (tokenMatch) {
    throw toFailure('GALLERY-TOKENS-IN-INTERFACE',
      `gallery-concept token in seam interface: "${tokenMatch[0]}"`);
  }

  for (const step of REQUIRED_STEPS) {
    emit(`${step}: PASS`);
  }
  return { ok: true, stats: stats || { presented, dropped: 0, deadlineMissed: 0 } };
}

module.exports = {
  parseSurfaceContractArgs,
  runSurfaceContract,
};
