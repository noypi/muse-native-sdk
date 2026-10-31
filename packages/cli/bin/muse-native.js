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

if (process.argv.slice(2)[0] !== 'gallery-proof' && process.argv.slice(2)[0] !== 'gallery-present' && process.argv.slice(2)[0] !== 'gallery-parity' && process.argv.slice(2)[0] !== 'gallery-tree') {
  require('../lib/cli.js');
}
