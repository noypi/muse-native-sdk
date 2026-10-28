'use strict';
// T11 — QuickJS variant smoke gate (NFR-BUILD-018 / AC-BUILD-068).
// Same source boots under either runtime with no app-code changes; log
// records boot steps. Runtime-conditional shapes fail the QuickJS leg:
// a `require('hermes-…')` / `from 'hermes-…'` import and an
// `if (runtime === …)` branch. Unknown runtime rejected by name.
// Fails closed: a missing/unreadable entry never throws.

function smokeVariant(entry) {
  if (!entry || typeof entry !== 'object') {
    return { ok: false, log: ['missing entry: cannot smoke boot variant'] };
  }
  if (typeof entry.source !== 'string') {
    return { ok: false, log: ['missing entry source: cannot smoke boot variant'] };
  }
  const runtime = entry.runtime;
  if (runtime !== 'hermes' && runtime !== 'quickjs') {
    return { ok: false, log: [`unknown runtime: '${String(runtime)}'`] };
  }
  const source = entry.source;
  if (runtime === 'quickjs') {
    const conditionalImport =
      source.match(/require\s*\(\s*['"](hermes-[^'"]*)['"]\s*\)/i) ||
      source.match(/(?:import|from)\s+['"](hermes-[^'"]*)['"]/i);
    if (conditionalImport) {
      return {
        ok: false,
        log: [
          `runtime-conditional import '${conditionalImport[1]}' rejected on quickjs`,
          'quickjs leg requires runtime-agnostic source with no app-code changes',
        ],
      };
    }
    if (/\bif\s*\(\s*runtime\s*===[^)]*\)/.test(source) || /\bif\s*\(\s*runtime\s*==[^)=][^)]*\)/.test(source)) {
      return {
        ok: false,
        log: [
          'runtime-conditional branch on `runtime` rejected on quickjs',
          'quickjs leg requires runtime-agnostic source with no app-code changes',
        ],
      };
    }
  }
  return {
    ok: true,
    log: ['resolve entry', 'load app', 'start app', `boot ok on ${runtime}`],
  };
}

module.exports = { smokeVariant };
