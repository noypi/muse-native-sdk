'use strict';
// T11b — FR-DX-005 / AC-DX-022 / TC-DX-022.
// Prefix completion over the component + design-token registries (data only).

function fail(code, cause, action) {
  const err = new Error(cause);
  err.code = code;
  err.cause = cause;
  err.action = action;
  throw err;
}

function complete(prefix, registries) {
  if (typeof prefix !== 'string') {
    fail(
      'E_CODEGEN_BAD_PREFIX',
      'DX-005: prefix must be a string',
      'Pass a string prefix to complete.',
    );
  }
  if (!registries || typeof registries !== 'object' || Array.isArray(registries)) {
    fail(
      'E_CODEGEN_BAD_REGISTRIES',
      'DX-005: registries must be an object with components and tokens',
      'Pass a registries envelope shaped { components: [{ name, props?, events?, enums? }], tokens?: [string] } to complete.',
    );
  }
  const components = Array.isArray(registries.components) ? registries.components : [];
  const tokens = Array.isArray(registries.tokens) ? registries.tokens : [];

  const candidates = [];
  for (const component of components) {
    if (!component || typeof component !== 'object') continue;
    const name = typeof component.name === 'string' ? component.name : 'unknown';
    const props = component.props && typeof component.props === 'object' ? component.props : {};
    for (const propName of Object.keys(props)) {
      candidates.push({ label: propName, kind: 'prop', doc: `Prop \`${propName}\` of \`${name}\` (DX-005).` });
    }
    const events = Array.isArray(component.events) ? component.events : [];
    for (const event of events) {
      const eName = event && event.name;
      if (typeof eName !== 'string' || eName.length === 0) continue;
      candidates.push({ label: eName, kind: 'event', doc: `Event \`${eName}\` of \`${name}\` (DX-005).` });
    }
    const enums = Array.isArray(component.enums) ? component.enums : [];
    for (const en of enums) {
      const values = en && Array.isArray(en.values) ? en.values : [];
      const eName = en && typeof en.name === 'string' ? en.name : 'unknown';
      for (const value of values) {
        const label = String(value);
        candidates.push({ label, kind: 'enum', doc: `Enum value \`${label}\` of \`${eName}\` on \`${name}\` (DX-005).` });
      }
    }
  }
  for (const token of tokens) {
    const label = String(token);
    candidates.push({ label, kind: 'token', doc: `Design token \`${label}\` (DX-005).` });
  }

  const needle = prefix.toLowerCase();
  const seen = new Set();
  const out = [];
  for (const item of candidates) {
    if (!item.label.toLowerCase().includes(needle)) continue;
    const key = `${item.kind}:${item.label}`;
    if (seen.has(key)) continue;
    seen.add(key);
    out.push(item);
  }
  return out;
}

module.exports = { complete };
