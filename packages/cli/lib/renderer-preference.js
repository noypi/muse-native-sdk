'use strict';
// renderer: preference reader — AC-GRAPHICS-041.
// Pure parse/reporting only: no probing, no env/config reads. Probe
// downgrade semantics for unavailable paths live in resolveEffectivePath
// (FR-GRAPHICS-014 runner shape stays in T2).

const KNOWN = ['cpu', 'egl', 'skia'];

function unknownValueError(raw) {
  const shown = typeof raw === 'string' ? JSON.stringify(raw) : String(raw);
  return new Error(`renderer: unknown value ${shown} (expected cpu|egl|skia)`);
}

function parseRendererPreference(raw) {
  if (raw == null || raw === '') {
    return { requested: 'cpu', effective: 'cpu', downgraded: false, trigger: '' };
  }
  if (typeof raw !== 'string' || KNOWN.indexOf(raw) < 0) {
    throw unknownValueError(raw);
  }
  return { requested: raw, effective: raw, downgraded: false, trigger: '' };
}

function resolveEffectivePath(requested, probe) {
  if (typeof requested !== 'string' || KNOWN.indexOf(requested) < 0) {
    throw unknownValueError(requested);
  }
  if (requested === 'cpu') {
    return { effective: 'cpu', trigger: '' };
  }
  const available = Boolean(probe && probe[`${requested}Available`]);
  if (available) {
    return { effective: requested, trigger: '' };
  }
  return { effective: 'cpu', trigger: `${requested} unavailable, downgraded to cpu` };
}

module.exports = {
  parseRendererPreference,
  resolveEffectivePath,
};
