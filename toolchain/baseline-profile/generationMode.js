'use strict';

// T3 — Profile generation requires release-mode R8-minified build
// (NFR-BUILD-006 / AC-BUILD-007).
// Minified names are what ship: generation is allowed only for an exact
// release-mode, minified build. Everything else fails closed with a reason
// naming the offending field.

function generationAllowed(build) {
  const descriptor = build !== null && typeof build === 'object' ? build : {};
  if (descriptor.mode !== 'release') {
    return { ok: false, reason: "mode must be 'release'" };
  }
  if (descriptor.minified !== true) {
    return { ok: false, reason: 'minified must be true' };
  }
  return { ok: true, reason: 'release minified build' };
}

module.exports = { generationAllowed };
