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

if (process.argv.slice(2)[0] !== 'gallery-proof') {
  require('../lib/cli.js');
}
