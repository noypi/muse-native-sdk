'use strict';
// T8 — Upload gate (NFR-BUILD-015 / AC-BUILD-051).
// Refuses unsigned, debug-signed, or non-reproducible release builds.

function uploadGate({ signed, debugSigned, reproducible }) {
  const reasons = [];
  if (!signed) {
    reasons.push('build is unsigned: release must be signed with a release key');
  }
  if (debugSigned) {
    reasons.push('build is debug-signed: release must not use a debug key');
  }
  if (!reproducible) {
    reasons.push('build is not reproducible: release must be reproducible');
  }
  return { ok: reasons.length === 0, reasons };
}

module.exports = { uploadGate };
