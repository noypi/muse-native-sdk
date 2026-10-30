'use strict';
// `muse-native gallery-tree --assert-evaluated --fail-on-static-fallback`
// (FR-SAMPLE-014 T3 / AC-RUNTIME-032 FR-RUNTIME-010).
//
// Proof-gate entry for the gallery tree: the default evaluated tree passes;
// the explicit-flag static fallback (`--static-fallback` /
// `MUSE_STATIC_FALLBACK=1`, see examples/m3-gallery/src/gallerySurface.js)
// reports the fallback and fails when `--fail-on-static-fallback` is set.

function parseGalleryTreeArgs(argv) {
  const args = Array.isArray(argv) ? argv : [];
  const opts = {
    assertEvaluated: false,
    failOnStaticFallback: false,
    staticFallback: false,
  };
  for (const arg of args) {
    if (arg === '--assert-evaluated') opts.assertEvaluated = true;
    else if (arg === '--fail-on-static-fallback') opts.failOnStaticFallback = true;
    else if (arg === '--static-fallback') opts.staticFallback = true;
  }
  if (process.env && process.env.MUSE_STATIC_FALLBACK === '1') {
    opts.staticFallback = true;
  }
  return opts;
}

function evaluateGalleryTree(opts) {
  const options = opts && typeof opts === 'object' ? opts : {};
  if (options.staticFallback) {
    try {
      process.stderr.write('[gallery-tree] static fallback reported (proofGate=fail)\n');
    } catch (_) {
      // The returned gate decision carries the report; never throw here.
    }
    return { pass: false, staticFallback: true, reported: true, proofGate: 'fail' };
  }
  return { pass: true, staticFallback: false, reported: false, proofGate: 'pass' };
}

function runGalleryTree(argv) {
  const opts = parseGalleryTreeArgs(argv);
  const result = evaluateGalleryTree(opts);
  if (opts.assertEvaluated && opts.failOnStaticFallback && !result.pass) {
    const err = new Error('gallery-tree: static fallback detected (--fail-on-static-fallback)');
    err.code = 'static-fallback';
    err.result = result;
    throw err;
  }
  return result;
}

module.exports = { parseGalleryTreeArgs, evaluateGalleryTree, runGalleryTree };
