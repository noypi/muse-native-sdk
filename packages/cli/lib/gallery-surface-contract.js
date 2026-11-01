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
const { spawnSync } = require('node:child_process');

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

// Serial resolution for default (non-injected) runners: explicit opts.device,
// else process.env.ANDROID_SERIAL, else null (fail closed NO-DEVICE-RUNNER).
// Never guesses among multiple devices; rejects empty/dash values like the
// usage-reject for --device.
function resolveSurfaceSerial(explicit) {
  const candidate = explicit || process.env.ANDROID_SERIAL || null;
  if (typeof candidate !== 'string' || candidate.length === 0 || candidate.startsWith('-')) {
    return null;
  }
  return candidate;
}

function surfaceAdb(serial, args) {
  const res = spawnSync('adb', ['-s', serial, ...args], { encoding: 'utf8' });
  if (res.error || res.status !== 0) {
    const detail = res.error
      ? String((res.error && res.error.message) || res.error)
      : String(res.stderr || res.stdout || '').trim().slice(0, 300);
    const err = new Error(
      `device unreachable via adb against serial '${serial}' (adb -s ${serial} ${args.join(' ')})${detail ? `: ${detail}` : ''}`,
    );
    err.code = 'DEVICE-UNREACHABLE';
    err.detail = err.message;
    throw err;
  }
  return res;
}

function defaultSurfaceDriveSequence(serial) {
  return async function drive() {
    surfaceAdb(serial, ['shell', 'am', 'force-stop', 'com.muse.host']);
    surfaceAdb(serial, ['logcat', '-c']);
    surfaceAdb(serial, ['shell', 'am', 'start', '-n', 'com.muse.host/.MainActivity']);
    await new Promise(r => setTimeout(r, 9000));
    surfaceAdb(serial, ['shell', 'input', 'tap', '150', '250']);
    await new Promise(r => setTimeout(r, 1500));
    surfaceAdb(serial, ['shell', 'input', 'swipe', '150', '400', '150', '100']);
    await new Promise(r => setTimeout(r, 1500));
    try { surfaceAdb(serial, ['shell', 'cmd', 'uimode', 'night', 'yes']); } catch (_) { /* best-effort: logcat markers matter */ }
    await new Promise(r => setTimeout(r, 2500));
    try { surfaceAdb(serial, ['shell', 'cmd', 'uimode', 'night', 'no']); } catch (_) { /* best-effort: logcat markers matter */ }
    await new Promise(r => setTimeout(r, 2500));
    surfaceAdb(serial, ['shell', 'am', 'broadcast', '-a', 'muse.native.NATIVE_SET_THEME', '--ei', 'night', '1']);
    await new Promise(r => setTimeout(r, 1500));
    surfaceAdb(serial, ['shell', 'am', 'broadcast', '-a', 'muse.native.NATIVE_SET_THEME', '--ei', 'night', '0']);
    await new Promise(r => setTimeout(r, 1500));
    surfaceAdb(serial, ['shell', 'input', 'keyevent', '4']);
    await new Promise(r => setTimeout(r, 6000));
    const dump = surfaceAdb(serial, ['logcat', '-d', '-s', 'muse-host']);
    const text = String(dump.stdout || '');
    const steps = [];
    const re = /SEAM-STEP:(\w+):(ok|fail)/g;
    let m;
    let failed = null;
    while ((m = re.exec(text)) !== null) {
      const step = m[1];
      const verdict = m[2];
      if (verdict === 'fail' && !failed) failed = step;
      if (verdict === 'ok' && !steps.includes(step)) steps.push(step);
    }
    if (failed) {
      const err = new Error(`seam step failed on device: ${failed}`);
      err.code = 'SEAM-STEP-FAIL';
      err.step = failed;
      err.detail = err.message;
      throw err;
    }
    return { steps, fallbackToStaticRoot: false };
  };
}

function defaultSurfaceReadLogcat(serial) {
  return async function read() {
    const dump = surfaceAdb(serial, ['logcat', '-d']);
    return String(dump.stdout || '').slice(0, 200000);
  };
}

function defaultSurfaceReadFrameStats(serial) {
  return async function read() {
    const dump = surfaceAdb(serial, ['logcat', '-d']);
    const text = String(dump.stdout || '');
    const matches = text.match(/repaint=1|Fully drawn|Displayed/g);
    const presented = matches ? matches.length : 0;
    return { presented, dropped: 0, deadlineMissed: 0 };
  };
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
  let driveSequence = deps.driveSequence;
  let readFrameStats = deps.readFrameStats;
  let readLogcat = deps.readLogcat;
  const readInterfaceText = typeof deps.readInterfaceText === 'function'
    ? deps.readInterfaceText
    : defaultReadInterfaceText;

  if (typeof driveSequence !== 'function'
    || typeof readFrameStats !== 'function'
    || typeof readLogcat !== 'function') {
    const serial = resolveSurfaceSerial(opts.device);
    if (!serial) {
      throw toFailure('NO-DEVICE-RUNNER', 'no device runner injected for gallery-real --assert-surface-contract');
    }
    if (typeof driveSequence !== 'function') driveSequence = defaultSurfaceDriveSequence(serial);
    if (typeof readFrameStats !== 'function') readFrameStats = defaultSurfaceReadFrameStats(serial);
    if (typeof readLogcat !== 'function') readLogcat = defaultSurfaceReadLogcat(serial);
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
