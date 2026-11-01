#!/usr/bin/env node
'use strict';
// muse-native gallery-proof --tiers --themes --assert-counts --no-blank [--out <dir>] [--apk <path>]
// T3 device proof entry (AC-M3-020 / TC-M3-021). Intercepted here so the
// generic option validation in lib/cli.js (which only knows the build flags)
// never sees the proof flags.
(function galleryProofEntry() {
  const argv = process.argv.slice(2);
  if (argv[0] !== 'gallery-proof') return;
  const proof = require('../lib/gallery-proof.js');
  if (argv.includes('--help') || argv.includes('-h')) {
    process.stdout.write(
      'Usage: muse-native gallery-proof --tiers --themes --assert-counts --no-blank [--out <dir>] [--apk <path>]\n',
    );
    return;
  }
  let opts;
  try {
    opts = proof.parseGalleryProofArgs(argv);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-proof --tiers --themes --assert-counts --no-blank [--out <dir>] [--apk <path>]\n`);
    process.exitCode = 2;
    return;
  }
  proof.runGalleryProof({
    serial: process.env.ANDROID_SERIAL,
    androidJar: process.env.ANDROID_JAR,
    outDir: opts.outDir,
    apkPath: opts.apkPath || undefined,
  }).then(
    (result) => {
      process.stdout.write(`gallery-proof ok: rows=${result.rows} pages=${result.pages} shots=${result.shots.length}\n`);
    },
    (err) => {
      process.stderr.write(`${(err && err.message) || String(err)}\n`);
      process.exitCode = 1;
    },
  );
})();

(function galleryPresentEntry() {
  const argv = process.argv.slice(2);
  if (argv[0] !== 'gallery-present') return;
  const present = require('../lib/gallery-present.js');
  if (argv.includes('--help') || argv.includes('-h')) {
    process.stdout.write(
      'Usage: muse-native gallery-present [--path egl|cpu|skia] [--dump-frame <png>]\n',
    );
    return;
  }
  let opts;
  try {
    opts = present.parseGalleryPresentArgs(argv);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-present [--path egl|cpu|skia] [--dump-frame <png>]\n`);
    process.exitCode = 2;
    return;
  }
  present.runGalleryPresent({
    serial: process.env.ANDROID_SERIAL,
    path: opts.path,
    dumpFrame: opts.dumpFrame || undefined,
  }).then(
    (result) => {
      if (result && result.frame) {
        process.stdout.write(`gallery-present ok: path=${result.path} frame=${result.frame}\n`);
      } else {
        process.stdout.write(`gallery-present ok: path=${result.path}\n`);
      }
    },
    (err) => {
      process.stderr.write(`${(err && err.message) || String(err)}\n`);
      process.exitCode = 1;
    },
  );
})();

(function galleryParityEntry() {
  const argv = process.argv.slice(2);
  if (argv[0] !== 'gallery-parity') return;
  const parity = require('../lib/gallery-parity.js');
  if (argv.includes('--help') || argv.includes('-h')) {
    process.stdout.write(
      'Usage: muse-native gallery-parity (--cpu-vs-egl|--cpu-vs-skia) --assert-md5\n',
    );
    return;
  }
  let opts;
  try {
    opts = parity.parseGalleryParityArgs(argv);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-parity (--cpu-vs-egl|--cpu-vs-skia) --assert-md5\n`);
    process.exitCode = 2;
    return;
  }
  if ((!opts.cpuVsEgl && !opts.cpuVsSkia) || !opts.assertMd5) {
    const missing = (!opts.cpuVsEgl && !opts.cpuVsSkia) ? '--cpu-vs-egl|--cpu-vs-skia' : '--assert-md5';
    process.stderr.write(`Error: missing required flag ${missing}\nUsage: muse-native gallery-parity (--cpu-vs-egl|--cpu-vs-skia) --assert-md5\n`);
    process.exitCode = 2;
    return;
  }
  parity.runGalleryParity({
    serial: opts.serial || process.env.ANDROID_SERIAL,
    assertMd5: true,
    cpuVsSkia: Boolean(opts.cpuVsSkia),
  }).then(
    (result) => {
      if (result.skiaMd5 !== undefined) {
        process.stdout.write(`gallery-parity ok: cpuMd5=${result.cpuMd5} skiaMd5=${result.skiaMd5} equal=${result.equal}\n`);
      } else {
        process.stdout.write(`gallery-parity ok: cpuMd5=${result.cpuMd5} eglMd5=${result.eglMd5} equal=${result.equal}\n`);
      }
      if (!result.equal) {
        process.exitCode = 1;
      }
    },
    (err) => {
      process.stderr.write(`${(err && err.message) || String(err)}\n`);
      process.exitCode = 1;
    },
  );
})();

// muse-native gallery-tree --assert-evaluated --fail-on-static-fallback [--static-fallback]
// FR-SAMPLE-014 T3 proof-gate entry (AC-RUNTIME-032). Delegates to the
// single-source dispatch in lib/cli.js so the flag handling lives in exactly
// one place (the generic option validation there only knows build flags,
// hence this intercept).
(function galleryTreeEntry() {
  const argv = process.argv.slice(2);
  if (argv[0] !== 'gallery-tree') return;
  require('../lib/cli.js').runGalleryTreeCommand(argv.slice(1));
})();

// muse-native gallery-real --assert-surface-contract [--device <serial>]
// FR-RUNTIME-011 T3 surface-contract gate (AC-RUNTIME-034, TC-RUNTIME-034).
// Intercepted here so the generic option validation in lib/cli.js (which
// only knows the build flags) never sees the gate flags.
(function galleryRealEntry() {
  const argv = process.argv.slice(2);
  if (argv[0] !== 'gallery-real') return;
  // FR-M3-011 dispatch: route the gallery-real flags to lib/gallery-real.js.
  // The surface-contract gate keeps its byte-for-byte behavior for its own
  // flag; anything carrying a FR-M3-011 flag (and no --assert-surface-contract)
  // delegates to the gallery-real module, which fails closed with usage
  // (exit 2) on unknown flags.
  const rest = argv.slice(1);
  const hasSurfaceContract = rest.includes('--assert-surface-contract');
  // T2 interactive gate (AC-M3-026/AC-GRAPHICS-043): `--assert-interactive`
  // runs through lib/gallery-surface-contract.js with the same
  // argv-array-only shell-out discipline as gallery-tap.js. Help lists the
  // flag (exit 0); device-less runs fail closed (exit 1 NO-DEVICE-RUNNER).
  if (rest.includes('--assert-interactive') && !hasSurfaceContract) {
    const contract = require('../lib/gallery-surface-contract.js');
    if (argv.includes('--help') || argv.includes('-h')) {
      process.stdout.write(
        'Usage: muse-native gallery-real --assert-interactive [--device <serial>]\n',
      );
      return;
    }
    let parsed;
    try {
      parsed = contract.parseInteractiveArgs(argv);
    } catch (err) {
      process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-real --assert-interactive [--device <serial>]\n`);
      process.exitCode = 2;
      return;
    }
    contract.runGalleryRealInteractive(argv, {
      device: parsed.device || process.env.ANDROID_SERIAL || null,
    }).then(
      (result) => {
        if (result && result.exitCode === 2) {
          process.stderr.write(`${result.stderr || 'usage'}\nUsage: muse-native gallery-real --assert-interactive [--device <serial>]\n`);
          process.exitCode = 2;
          return;
        }
        if (result && result.exitCode !== 0) {
          process.stderr.write(`${(result && result.stderr) || `INTERACTIVE-FAIL: ${result.code}: ${result.detail}`}\n`);
          process.exitCode = 1;
          return;
        }
      },
      (err) => {
        const code = (err && typeof err.code === 'string' && err.code.length > 0) ? err.code : 'INTERACTIVE-FAIL';
        const detail = (err && typeof err.detail === 'string' && err.detail.length > 0)
          ? err.detail
          : ((err && err.message) || String(err));
        process.stderr.write(`INTERACTIVE-FAIL: ${code}: ${detail}\n`);
        process.exitCode = 1;
      },
    );
    return;
  }
  const hasGalleryRealFlag = rest.includes('--assert-no-diagram')
    || rest.includes('--assert-counts')
    || rest.includes('--assert-interactive')
    || rest.includes('--assert-real-pixels')
    || rest.includes('--page');
  if (hasGalleryRealFlag && !hasSurfaceContract) {
    const real = require('../lib/gallery-real.js');
    real.runGalleryReal(rest);
    return;
  }
  const contract = require('../lib/gallery-surface-contract.js');
  if (argv.includes('--help') || argv.includes('-h')) {
    process.stdout.write(
      'Usage: muse-native gallery-real --assert-surface-contract [--device <serial>]\n',
    );
    return;
  }
  let opts;
  try {
    opts = contract.parseSurfaceContractArgs(argv);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-real --assert-surface-contract [--device <serial>]\n`);
    process.exitCode = 2;
    return;
  }
  contract.runSurfaceContract({
    device: opts.device || process.env.ANDROID_SERIAL || null,
  }).then(
    () => {},
    (err) => {
      const code = (err && typeof err.code === 'string' && err.code.length > 0) ? err.code : 'SURFACE-CONTRACT-FAIL';
      const detail = (err && typeof err.detail === 'string' && err.detail.length > 0)
        ? err.detail
        : ((err && err.message) || String(err));
      process.stderr.write(`SURFACE-CONTRACT-FAIL: ${code}: ${detail}\n`);
      process.exitCode = 1;
    },
  );
})();

// muse-native gallery-tap --tap-page [--assert-zero-jni-faults] [--device <serial>]
// FR-RUNTIME-012 T6 tap-page gate (AC-RUNTIME-036/037, TC-RUNTIME-036/037).
// Intercepted here (like the T3 gallery-real gate above) so the generic
// `--help` handling in lib/cli.js never swallows the tap-page help text.
// Button-lane invocations (no `--tap-page`) fall through to lib/cli.js.
(function galleryTapPageEntry() {
  const argv = process.argv.slice(2);
  if (argv[0] !== 'gallery-tap' || !argv.includes('--tap-page')) return;
  const tapPage = require('../lib/gallery-tap.js');
  if (argv.includes('--help') || argv.includes('-h')) {
    process.stdout.write(
      'Usage: muse-native gallery-tap --tap-page [--assert-zero-jni-faults] [--device <serial>]\n',
    );
    return;
  }
  let opts;
  try {
    opts = tapPage.parseTapPageArgs(argv);
  } catch (err) {
    process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-tap --tap-page [--assert-zero-jni-faults] [--device <serial>]\n`);
    process.exitCode = 2;
    return;
  }
  tapPage.runTapPage({
    device: opts.device || process.env.ANDROID_SERIAL || null,
    assertZeroJniFaults: opts.assertZeroJniFaults,
  }).then(
    () => {},
    (err) => {
      const code = (err && typeof err.code === 'string' && err.code.length > 0) ? err.code : 'TAP-PAGE-FAIL';
      const detail = (err && typeof err.detail === 'string' && err.detail.length > 0)
        ? err.detail
        : ((err && err.message) || String(err));
      process.stderr.write(`TAP-PAGE-FAIL: ${code}: ${detail}\n`);
      process.exitCode = 1;
    },
  );
})();

if (process.argv.slice(2)[0] !== 'gallery-proof' && process.argv.slice(2)[0] !== 'gallery-present' && process.argv.slice(2)[0] !== 'gallery-parity' && process.argv.slice(2)[0] !== 'gallery-tree' && process.argv.slice(2)[0] !== 'gallery-real' && !(process.argv.slice(2)[0] === 'gallery-tap' && process.argv.slice(2).includes('--tap-page'))) {
  require('../lib/cli.js');
}
