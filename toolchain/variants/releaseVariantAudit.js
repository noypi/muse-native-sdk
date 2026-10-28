'use strict';
// T10 — release variant audit gate (NFR-BUILD-018 / AC-BUILD-067).
// Fails closed: any validation-layer symbol or debug marker is a violation
// naming it; a missing/uninspectable descriptor fails closed, never throws.

function auditReleaseVariant(artifact) {
  if (!artifact || typeof artifact !== 'object') {
    return { ok: false, violations: ['missing artifact descriptor: cannot audit release variant'] };
  }
  const violations = [];
  const symbols = artifact.validationLayerSymbols;
  if (Array.isArray(symbols)) {
    for (const sym of symbols) {
      violations.push(`validation-layer symbol present: '${String(sym)}'`);
    }
  } else if (symbols !== undefined) {
    violations.push(`invalid validationLayerSymbols: '${String(symbols)}'`);
  }
  if (artifact.debugMarkers) {
    violations.push('debug markers present in release variant');
  }
  return { ok: violations.length === 0, violations };
}

module.exports = { auditReleaseVariant };
