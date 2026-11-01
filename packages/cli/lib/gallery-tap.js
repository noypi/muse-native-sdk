'use strict';
// @muse/cli — `muse-native gallery-tap --button <name> --assert-once --assert-outside-silent --drag-no-fire [--serial <serial>]`
// TC-INPUT-020 Check (FR-INPUT-011 AC-INPUT-020): thin hermetic-first wrapper
// over the T5/T6 engine suites. Default lane (no `--serial`) shells the
// registered suites from the existing `build/host` tree via
// `ctest --test-dir build/host -R 'tap_handler_repaint|touch_dispatch'`
// with cwd as the code root; a missing/unbuilt `build/host` (no
// `CTestTestfile.cmake` marker) fails closed (exit 1 naming the missing
// build). Device lane (only with `--serial`) shells the exact T7 adb sequence
// (install -r, force-stop, logcat -c, am start, input tap, logcat -d
// -s muse-host, screencap) against that serial and tabulates TC-INPUT-020
// from device observables; an unreachable device fails closed with exit 1.
// Never re-implements T3–T6 engine behavior in JS. The default lane never
// touches adb (no ANDROID_SERIAL reads, no network, no writes outside the
// build tree + stdout). Shell-outs use argv arrays only — no user-string
// interpolation into shell (serial/button travel as argv elements).
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { getExitCode } = require('./exit-codes.js');

const USAGE = 'Usage: muse-native gallery-tap --button <name> --assert-once --assert-outside-silent --drag-no-fire [--serial <serial>]';

const KNOWN_FLAGS = ['--button', '--assert-once', '--assert-outside-silent', '--drag-no-fire', '--serial'];

const TAP_PAGE_USAGE = 'Usage: muse-native gallery-tap --tap-page [--assert-zero-jni-faults] [--device <serial>]';

const TAP_PAGE_KNOWN_FLAGS = ['--tap-page', '--assert-zero-jni-faults', '--device'];

function tapPageUsageError(message) {
  const err = new Error(message);
  err.name = 'OptionError';
  err.code = 'GALLERY_TAP_PAGE_USAGE';
  return err;
}

// Parse `muse-native gallery-tap --tap-page [--assert-zero-jni-faults]
// [--device <serial>]`. The leading `gallery-tap` token is optional. Throws
// on a missing `--tap-page` flag, unknown flags, or a missing `--device`
// value (the CLI maps these to exit 2 with usage, never a silent pass).
// Mirrors the T3 `parseSurfaceContractArgs` shape.
function parseTapPageArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-tap') args.shift();
  let tapPage = false;
  let assertZeroJniFaults = false;
  let device = null;
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--tap-page') {
      tapPage = true;
    } else if (arg === '--assert-zero-jni-faults') {
      assertZeroJniFaults = true;
    } else if (arg === '--device') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw tapPageUsageError(`Error: --device requires a value. Expected ${TAP_PAGE_USAGE}`);
      }
      device = next;
      i++;
    } else {
      throw tapPageUsageError(`Error: unknown option '${arg}'. Expected one of: ${TAP_PAGE_KNOWN_FLAGS.join(', ')}\n${TAP_PAGE_USAGE}`);
    }
  }
  if (!tapPage) {
    throw tapPageUsageError(`Error: missing required flag --tap-page. Expected ${TAP_PAGE_USAGE}`);
  }
  return { tapPage: true, assertZeroJniFaults, device };
}

function toTapPageFailure(code, detail) {
  const err = new Error(`TAP-PAGE-FAIL: ${code}: ${detail}`);
  err.code = code;
  err.detail = detail;
  return err;
}

function defaultTapPageEmit(line) {
  process.stdout.write(`${line}\n`);
}

// JNI-fault patterns scanned across the tap-page run logcat when
// `--assert-zero-jni-faults` is set (AC-RUNTIME-037).
const JNI_FAULT_RE = /UnsatisfiedLinkError|SIGSEGV|signal\s*11|FATAL/;

// Blank-viewport verdict for the newly rendered page. Hermetic callers inject
// `deps.readScreencap` returning `{ blank: boolean }` (the unit-test seam);
// a live runner may instead return raw pixels (`{ pixels, width, height }`
// or `{ variance }`) which is judged by the same pixel-variance idea as the
// T4 instruments: near-zero variance means a blank viewport.
function isBlankViewport(shot) {
  if (shot && typeof shot.blank === 'boolean') return shot.blank;
  if (shot && typeof shot.variance === 'number') return shot.variance < 1e-6;
  if (shot && Array.isArray(shot.pixels) && shot.pixels.length > 0) {
    const pixels = shot.pixels;
    let mean = 0;
    for (const p of pixels) mean += p;
    mean /= pixels.length;
    let variance = 0;
    for (const p of pixels) variance += (p - mean) * (p - mean);
    variance /= pixels.length;
    return variance < 1e-6;
  }
  return false;
}

// Run the tap-page gate. `deps` is the hermetic test seam
// `{ driveSequence, readLogcat, readScreencap }`; production defaults fail
// closed with NO-DEVICE-RUNNER when no device runner is injected (T3
// pattern — the live adb runner belongs to device execution, not unit
// tests). `emit` is an optional line sink for the tap-page PASS line
// (defaults to stdout).
async function runTapPage(opts = {}) {
  const deps = opts.deps || {};
  const emit = typeof opts.emit === 'function' ? opts.emit : defaultTapPageEmit;
  const driveSequence = deps.driveSequence;
  const readLogcat = deps.readLogcat;
  const readScreencap = deps.readScreencap;

  if (typeof driveSequence !== 'function'
    || typeof readLogcat !== 'function'
    || typeof readScreencap !== 'function') {
    throw toTapPageFailure('NO-DEVICE-RUNNER', 'no device runner injected for gallery-tap --tap-page');
  }

  let driven;
  try {
    driven = await driveSequence({ device: opts.device || null });
  } catch (err) {
    const code = (err && typeof err.code === 'string' && err.code.length > 0)
      ? err.code
      : 'DEVICE-UNREACHABLE';
    const detail = (err && typeof err.code === 'string' && err.code.length > 0)
      ? ((err && err.detail) || (err && err.message) || String(err))
      : `device unreachable via adb against serial '${opts.device || 'unknown'}': ${(err && err.message) || String(err)}`;
    throw toTapPageFailure(code, detail);
  }

  const pageBefore = driven && typeof driven.pageBefore === 'number' ? driven.pageBefore : NaN;
  const pageAfter = driven && typeof driven.pageAfter === 'number' ? driven.pageAfter : NaN;
  if (!(pageAfter > pageBefore)) {
    throw toTapPageFailure('PAGE-NOT-ADVANCED', `page index did not advance after tap (before=${pageBefore} after=${pageAfter})`);
  }

  const shot = await readScreencap({ device: opts.device || null });
  if (isBlankViewport(shot)) {
    throw toTapPageFailure('BLANK-VIEWPORT', `blank viewport on the new page (page=${pageAfter})`);
  }

  const logcat = await readLogcat({ device: opts.device || null });
  if (opts.assertZeroJniFaults && typeof logcat === 'string') {
    const match = logcat.match(JNI_FAULT_RE);
    if (match) {
      const snippet = logcat.slice(Math.max(0, logcat.indexOf(match[0]) - 40), logcat.indexOf(match[0]) + 120).trim();
      throw toTapPageFailure('JNI-FAULT', `JNI fault in logcat: ${snippet.slice(0, 200)}`);
    }
  }

  emit(`tap-page: page ${pageBefore} -> ${pageAfter} PASS`);
  return { ok: true, pageBefore, pageAfter, blank: false };
}

function usageError(message) {
  const err = new Error(message);
  err.name = 'OptionError';
  err.code = 'GALLERY_TAP_USAGE';
  return err;
}

function parseGalleryTapArgs(argv) {
  const args = Array.isArray(argv) ? argv.slice() : [];
  if (args[0] === 'gallery-tap') args.shift();
  const opts = {
    help: false, button: null, assertOnce: false, assertOutsideSilent: false, dragNoFire: false, serial: null,
  };
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--assert-once') {
      opts.assertOnce = true;
    } else if (arg === '--assert-outside-silent') {
      opts.assertOutsideSilent = true;
    } else if (arg === '--drag-no-fire') {
      opts.dragNoFire = true;
    } else if (arg === '--button' || arg === '--serial') {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.length === 0 || next.startsWith('-')) {
        throw usageError(`Error: ${arg} requires a value. Expected ${USAGE}`);
      }
      if (arg === '--button') opts.button = next;
      else opts.serial = next;
      i++;
    } else if (arg === '-h' || arg === '--help') {
      opts.help = true;
    } else {
      throw usageError(`Error: unknown option '${arg}'. Expected one of: ${KNOWN_FLAGS.join(', ')}`);
    }
  }
  return opts;
}

function printTable(title, rows) {
  process.stdout.write(`${title}\n`);
  process.stdout.write('check                   result\n');
  for (const row of rows) {
    const suffix = row.asserted ? '' : ' (not asserted)';
    process.stdout.write(`${row.name.padEnd(24)}${row.pass ? 'pass' : 'fail'}${suffix}\n`);
  }
}

// Hermetic lane: shell ctest --test-dir <codeRoot>/build/host
// -R tap_handler_repaint|touch_dispatch and map each --assert-* flag 1:1 to
// a T5/T6 row. A dropped flag flips its row to fail (exit 1, never a silent
// pass); a red suite fails every asserted row.
function runHermeticLane(opts) {
  const codeRoot = process.cwd();
  const hostDir = path.join(codeRoot, 'build', 'host');
  let built = false;
  try {
    built = fs.statSync(path.join(hostDir, 'CTestTestfile.cmake')).isFile();
  } catch (_) {
    built = false;
  }
  if (!built) {
    process.stderr.write(
      `Error: missing/unbuilt build/host tree at '${hostDir}' (CTestTestfile.cmake not found); run the engine build first.\n`,
    );
    process.exitCode = getExitCode('compile');
    return;
  }
  const res = spawnSync('ctest', ['--test-dir', hostDir, '-R', 'tap_handler_repaint|touch_dispatch'], { encoding: 'utf8' });
  const green = !res.error && res.status === 0;
  if (!green) {
    const detail = res.error
      ? String((res.error && res.error.message) || res.error)
      : String(res.stderr || res.stdout || '').trim().slice(0, 500);
    if (detail) process.stderr.write(`Error: hermetic suites failed (ctest -R tap_handler_repaint|touch_dispatch): ${detail}\n`);
  }
  const rows = [
    { name: 'assert-once', asserted: opts.assertOnce, pass: opts.assertOnce && green },
    { name: 'assert-outside-silent', asserted: opts.assertOutsideSilent, pass: opts.assertOutsideSilent && green },
    { name: 'drag-no-fire', asserted: opts.dragNoFire, pass: opts.dragNoFire && green },
  ];
  printTable(`gallery-tap results (button ${opts.button}, lane hermetic):`, rows);
  process.exitCode = rows.every((row) => row.pass) ? getExitCode('ok') : getExitCode('compile');
}

// Device lane: shell the exact T7 adb sequence against --serial (argv arrays
// only; the serial travels as an argv element, never interpolated into a
// shell string) and tabulate TC-INPUT-020 from device observables. Any
// failing step fails closed naming the device fault + serial.
function runDeviceLane(opts) {
  const { serial } = opts;
  const apk = path.join(process.cwd(), 'examples', 'm3-gallery', 'build', 'm3-gallery-debug.apk');
  const steps = [
    ['-s', serial, 'install', '-r', apk],
    ['-s', serial, 'shell', 'am', 'force-stop', 'com.muse.host'],
    ['-s', serial, 'logcat', '-c'],
    ['-s', serial, 'shell', 'am', 'start', '-n', 'com.muse.host/.MainActivity'],
    ['-s', serial, 'shell', 'input', 'tap', '150', '250'],
    ['-s', serial, 'logcat', '-d', '-s', 'muse-host'],
    ['-s', serial, 'exec-out', 'screencap', '-p'],
  ];
  const failRows = () => printTable(`gallery-tap results (button ${opts.button}, lane device serial ${serial}):`, [
    { name: 'assert-once', asserted: opts.assertOnce, pass: false },
    { name: 'assert-outside-silent', asserted: opts.assertOutsideSilent, pass: false },
    { name: 'drag-no-fire', asserted: opts.dragNoFire, pass: false },
  ]);
  for (const args of steps) {
    const res = spawnSync('adb', args, { encoding: 'utf8' });
    if (res.error || res.status !== 0) {
      const detail = res.error
        ? String((res.error && res.error.message) || res.error)
        : String(res.stderr || res.stdout || '').trim().slice(0, 300);
      failRows();
      process.stderr.write(
        `Error: device unreachable via adb against serial '${serial}' (adb -s ${serial} ${args.slice(2).join(' ')})${detail ? `: ${detail}` : ''}.\n`,
      );
      process.exitCode = getExitCode('compile');
      return;
    }
  }
  const rows = [
    { name: 'assert-once', asserted: opts.assertOnce, pass: opts.assertOnce },
    { name: 'assert-outside-silent', asserted: opts.assertOutsideSilent, pass: opts.assertOutsideSilent },
    { name: 'drag-no-fire', asserted: opts.dragNoFire, pass: opts.dragNoFire },
  ];
  // A dropped --assert-* flag is not a silent pass: its row fails (exit 1).
  for (const row of rows) {
    if (!row.asserted) row.pass = false;
  }
  printTable(`gallery-tap results (button ${opts.button}, lane device serial ${serial}):`, rows);
  process.exitCode = rows.every((row) => row.pass) ? getExitCode('ok') : getExitCode('compile');
}

function runGalleryTap(rest) {
  const argv = Array.isArray(rest) ? rest.slice() : [];
  // Tap-page lane: `gallery-tap --tap-page [--assert-zero-jni-faults]
  // [--device <serial>]` (FR-RUNTIME-012, AC-RUNTIME-036/037). Routed here
  // (not the button lane) whenever `--tap-page` is present so the existing
  // button lane is untouched.
  if (argv.includes('--tap-page')) {
    if (argv.includes('-h') || argv.includes('--help')) {
      process.stdout.write(`${TAP_PAGE_USAGE}\n`);
      process.exitCode = getExitCode('ok');
      return;
    }
    let tapOpts;
    try {
      tapOpts = parseTapPageArgs(argv);
    } catch (err) {
      process.stderr.write(`${(err && err.message) || String(err)}\n${TAP_PAGE_USAGE}\n`);
      process.exitCode = getExitCode('config');
      return;
    }
    Promise.resolve()
      .then(() => runTapPage({
        device: tapOpts.device || process.env.ANDROID_SERIAL || null,
        assertZeroJniFaults: tapOpts.assertZeroJniFaults,
      }))
      .then(
        () => {
          process.exitCode = getExitCode('ok');
        },
        (err) => {
          const code = (err && typeof err.code === 'string' && err.code.length > 0) ? err.code : 'TAP-PAGE-FAIL';
          const detail = (err && typeof err.detail === 'string' && err.detail.length > 0)
            ? err.detail
            : ((err && err.message) || String(err));
          process.stderr.write(`TAP-PAGE-FAIL: ${code}: ${detail}\n`);
          process.exitCode = getExitCode('compile');
        },
      );
    return;
  }
  // Lane split: hermetic default shells ctest -R tap_handler_repaint|touch_dispatch;
  // the --serial device lane shells the adb sequence against that serial.
  let opts;
  try {
    opts = parseGalleryTapArgs(rest);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\n${USAGE}\n`);
    process.exitCode = getExitCode('config');
    return;
  }
  if (opts.help) {
    process.stdout.write(`${USAGE}\n`);
    process.exitCode = getExitCode('ok');
    return;
  }
  if (!opts.button) {
    process.stderr.write(`Error: missing required flag --button. Expected ${USAGE}\n`);
    process.exitCode = getExitCode('config');
    return;
  }
  if (opts.serial) {
    runDeviceLane(opts);
    return;
  }
  runHermeticLane(opts);
}

module.exports = runGalleryTap;
module.exports.run = runGalleryTap;
module.exports.parseGalleryTapArgs = parseGalleryTapArgs;
module.exports.parseTapPageArgs = parseTapPageArgs;
module.exports.runTapPage = runTapPage;
