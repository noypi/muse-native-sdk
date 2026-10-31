'use strict';

// Gallery mode selector (BUG-SAMPLE-003 T4 / AC-SAMPLE-041 flag clause /
// AC-RUNTIME-032 + TC-RUNTIME-032 fail-closed static fallback).
//
// Default path renders only manifest-generated real tags in themed sections
// (mode 'generated'). The static Showcase default export + LayeringPage
// render only when the explicit opt-in flag MUSE_GALLERY_STATIC_FALLBACK=1
// (env or launch/argv token) is set (mode 'static-fallback'); that path
// calls reportStaticFallback() and fails the proof gate
// (gallery-tree --assert-evaluated --fail-on-static-fallback) via
// assertNoStaticFallback().

const FLAG_NAME = 'MUSE_GALLERY_STATIC_FALLBACK';

function isFlagSet() {
  try {
    if (
      typeof process !== 'undefined' &&
      process.env &&
      (process.env[FLAG_NAME] === '1' || process.env[FLAG_NAME] === 'true')
    ) {
      return true;
    }
    if (
      typeof process !== 'undefined' &&
      Array.isArray(process.argv) &&
      process.argv.some((a) => String(a).includes(FLAG_NAME))
    ) {
      return true;
    }
  } catch {
    return false;
  }
  return false;
}

// Returns 'generated' unless the explicit static-fallback flag is set.
function getGalleryMode() {
  return isFlagSet() ? 'static-fallback' : 'generated';
}

// Explicit fallback report, called whenever the static root renders.
function reportStaticFallback() {
  const message =
    '[muse-gallery] static-fallback mode: rendering static Showcase ' +
    'via MUSE_GALLERY_STATIC_FALLBACK=1; generated manifest path bypassed (fallback)';
  try {
    if (typeof console !== 'undefined' && console && typeof console.warn === 'function') {
      console.warn(message);
    }
  } catch {
    // Reporting must never break rendering.
  }
  return message;
}

// Fail-closed proof gate for TC-RUNTIME-032:
// `muse-native gallery-tree --assert-evaluated --fail-on-static-fallback`
// exits non-zero in fallback mode. Throws in fallback mode, returns 0
// (pass) in generated mode.
function assertNoStaticFallback() {
  if (getGalleryMode() === 'static-fallback') {
    throw new Error(
      'gallery-tree --assert-evaluated --fail-on-static-fallback: static fallback active, failing proof gate',
    );
  }
  return 0;
}

module.exports = {
  FLAG_NAME,
  getGalleryMode,
  reportStaticFallback,
  assertNoStaticFallback,
  // Accepted aliases per contract ASSUMPTION A4.
  checkStaticFallback: assertNoStaticFallback,
  failOnStaticFallback: assertNoStaticFallback,
  assertNoStaticFallbackGate: assertNoStaticFallback,
};
