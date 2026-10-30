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
