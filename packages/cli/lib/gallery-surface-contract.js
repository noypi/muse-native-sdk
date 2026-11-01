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

function surfaceAdb(serial, args, opts) {
  const res = spawnSync('adb', ['-s', serial, ...args], {
    encoding: (opts && opts.encoding) || 'utf8',
  });
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

// Pre-drive screencap bytes keyed by serial, captured best-effort inside the
// default driveSequence so a frozen frame (post byte-identical to pre) fails
// the drive instead of passing as a contract hold.
const surfacePreDriveShots = new Map();

function defaultSurfaceDriveSequence(serial) {
  return async function drive(callArgs) {
    // Honor the call-time {device} when provided, else the bound serial.
    const s = (callArgs && typeof callArgs.device === 'string'
      && callArgs.device.length > 0 && !callArgs.device.startsWith('-'))
      ? callArgs.device
      : serial;
    surfaceAdb(s, ['shell', 'am', 'force-stop', 'com.muse.host']);
    surfaceAdb(s, ['logcat', '-c']);
    surfaceAdb(s, ['shell', 'am', 'start', '-n', 'com.muse.host/.MainActivity']);
    await new Promise(r => setTimeout(r, 9000));
    // Best-effort pre-drive shot for the frozen-frame comparison; failures
    // here must not fail the drive itself.
    try {
      const pre = surfaceAdb(s, ['exec-out', 'screencap', '-p'], { encoding: 'buffer' });
      if (pre && pre.stdout && pre.stdout.length > 0) {
        surfacePreDriveShots.set(s, Buffer.from(pre.stdout));
      }
    } catch (_) {
      // ignore — repaint evidence still gates the drive.
    }
    surfaceAdb(s, ['shell', 'input', 'tap', '150', '250']);
    await new Promise(r => setTimeout(r, 1500));
    surfaceAdb(s, ['shell', 'input', 'swipe', '150', '400', '150', '100']);
    await new Promise(r => setTimeout(r, 1500));
    // A device without `cmd uimode` is not a device we can prove insets on:
    // let adb failures throw DEVICE-UNREACHABLE (no swallow).
    surfaceAdb(s, ['shell', 'cmd', 'uimode', 'night', 'yes']);
    await new Promise(r => setTimeout(r, 2500));
    surfaceAdb(s, ['shell', 'cmd', 'uimode', 'night', 'no']);
    await new Promise(r => setTimeout(r, 2500));
    surfaceAdb(s, ['shell', 'am', 'broadcast', '-a', 'muse.native.NATIVE_SET_THEME', '--ei', 'night', '1']);
    await new Promise(r => setTimeout(r, 1500));
    surfaceAdb(s, ['shell', 'am', 'broadcast', '-a', 'muse.native.NATIVE_SET_THEME', '--ei', 'night', '0']);
    await new Promise(r => setTimeout(r, 1500));
    surfaceAdb(s, ['shell', 'input', 'keyevent', '4']);
    await new Promise(r => setTimeout(r, 6000));
    const dump = surfaceAdb(s, ['logcat', '-d', '-s', 'muse-host']);
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
    // Observed static-root fallback marker (authoritative host marker family:
    // STATIC-ROOT / "static root" / "fallback ... static"). Linear scan.
    const fallbackToStaticRoot = /static[^a-z]*root|fallback[^a-z]*static/i.test(text);
    // Repaint evidence: at least one repaint=1 line must be observed, else
    // the run presented nothing and is not a contract hold.
    const repaintMatches = text.match(/repaint=1/g);
    const framesAdvanced = Boolean(repaintMatches && repaintMatches.length > 0);
    if (!framesAdvanced) {
      const err = new Error('seam step failed on device: no repaint=1 evidence observed (present-less run)');
      err.code = 'SEAM-STEP-FAIL';
      err.step = 'render';
      err.detail = err.message;
      throw err;
    }
    // Frozen-frame check: post shot byte-identical to the pre shot means the
    // drive rendered nothing new. Best-effort (missing shots do not fail).
    try {
      const post = surfaceAdb(s, ['exec-out', 'screencap', '-p'], { encoding: 'buffer' });
      const pre = surfacePreDriveShots.get(s);
      surfacePreDriveShots.delete(s);
      const postBytes = post && post.stdout ? Buffer.from(post.stdout) : null;
      if (pre && pre.length > 0 && postBytes && postBytes.length > 0 && postBytes.equals(pre)) {
        const err = new Error('seam step failed on device: frozen frame (post-drive shot identical to pre-drive shot)');
        err.code = 'SEAM-STEP-FAIL';
        err.step = 'render';
        err.detail = err.message;
        throw err;
      }
    } catch (err) {
      if (err && err.code === 'SEAM-STEP-FAIL') throw err;
      // ignore post-shot capture failures — repaint evidence already gated.
    }
    return { steps, fallbackToStaticRoot, framesAdvanced };
  };
}

function defaultSurfaceReadLogcat(serial) {
  return async function read(callArgs) {
    const s = (callArgs && typeof callArgs.device === 'string'
      && callArgs.device.length > 0 && !callArgs.device.startsWith('-'))
      ? callArgs.device
      : serial;
    const dump = surfaceAdb(s, ['logcat', '-d']);
    return String(dump.stdout || '').slice(0, 200000);
  };
}

function defaultSurfaceReadFrameStats(serial) {
  return async function read(callArgs) {
    const s = (callArgs && typeof callArgs.device === 'string'
      && callArgs.device.length > 0 && !callArgs.device.startsWith('-'))
      ? callArgs.device
      : serial;
    // Tag-filtered dump so stale system-buffer lines never count.
    const dump = surfaceAdb(s, ['logcat', '-d', '-s', 'muse-host']);
    const text = String(dump.stdout || '');
    // Count only OUR package's lines (belt-and-braces on top of -s), and
    // only repaint=1 + Fully drawn/Displayed within those lines.
    let presented = 0;
    for (const line of text.split('\n')) {
      if (!/muse-host|com\.muse\.host/.test(line)) continue;
      const matches = line.match(/repaint=1|Fully drawn|Displayed/g);
      if (matches) presented += matches.length;
    }
    // Return { presented } only — no hardcoded dropped/deadlineMissed claims.
    return { presented };
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
  if (driven && driven.framesAdvanced === false) {
    throw toFailure('SEAM-STEP-FAIL', 'render: no repaint evidence observed (present-less run is not a contract hold)');
  }
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
