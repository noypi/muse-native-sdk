'use strict';
// FR-LAYOUT-008 / AC-LAYOUT-019 (T3) — CSS-to-token equivalence gate.
//
// `resolveStylesheet(css, theme)` returns the resolved property set for the
// supported subset (T1) with `var(--<token>)` references resolved through the
// T4 cascade (`engine/layout/tokens/store.js`). An unsupported property fails
// the build with a source-mapped `UnsupportedPropertyError`, never a silent
// no-op.

const { validateStylesheet } = require('./css-subset.js');
const store = require('../../../engine/layout/tokens/store.js');

class UnsupportedPropertyError extends Error {
  constructor({ property, line, column }) {
    super(`Unsupported property "${property}" at ${line}:${column}`);
    this.property = property;
    this.line = line;
    this.column = column;
  }
}

function resolveVarReferences(value, theme) {
  return value.replace(/var\(\s*--([A-Za-z0-9_.-]+)\s*\)/g, (match, tokenName) => {
    const resolved = store.resolveToken(tokenName, undefined, theme);
    return resolved.value;
  });
}

function resolveStylesheet(css, theme) {
  if (typeof css !== 'string' || css.length === 0) return {};
  if (css.trim() === '') return {};

  const errors = validateStylesheet(css);
  if (errors.length > 0) {
    throw new UnsupportedPropertyError(errors[0]);
  }

  const stripped = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const out = {};
  const declRe = /([A-Za-z][A-Za-z0-9-]*)\s*:\s*([^;{}]+?)(?:;|(?=\}))/g;
  let match;
  while ((match = declRe.exec(stripped)) !== null) {
    const property = match[1];
    const rawValue = match[2].trim();
    if (rawValue.length === 0) continue;
    out[property] = resolveVarReferences(rawValue, theme);
  }
  return out;
}

module.exports = { resolveStylesheet, UnsupportedPropertyError };
