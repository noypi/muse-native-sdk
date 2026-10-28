'use strict';

// T7 — R8 full mode + code/resource shrinking flags (NFR-BUILD-007 / AC-BUILD-011).
// Release-only: debug builds are never used for size/startup claims. Every
// size/startup measurement must come from a release build with the flags below.

const REQUIRED_FLAGS = [
  'android.enableR8.fullMode',
  'android.enableMinify',
  'android.shrinkResources',
];

function r8ConfigValid(props) {
  if (props === null || typeof props !== 'object' || Array.isArray(props)) {
    return { ok: false, violations: ['props: non-object input cannot prove a release R8 config'] };
  }
  const violations = [];
  for (const key of REQUIRED_FLAGS) {
    if (props[key] !== 'true') {
      violations.push(`${key}: expected 'true' (got ${JSON.stringify(props[key])})`);
    }
  }
  return { ok: violations.length === 0, violations };
}

module.exports = { r8ConfigValid };
