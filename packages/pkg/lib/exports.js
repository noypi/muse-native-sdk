'use strict';

// Declared-exports binding (FR-PKG-003, AC-PKG-006/008).
// Binds ONLY manifest-listed symbols to their callable functions; unlisted
// library symbols never reach the JS surface. Pure manifest-vs-table
// comparison; never touches native `.so` / `dlopen`.

const { PkgError } = require('./errors.js');

function isRecord(value) {
  return typeof value === 'object' && value !== null;
}

function sameKinds(actual, expected) {
  if (!Array.isArray(actual) || !Array.isArray(expected)) return false;
  if (actual.length !== expected.length) return false;
  for (let i = 0; i < expected.length; i += 1) {
    if (actual[i] !== expected[i]) return false;
  }
  return true;
}

function checkSymbol(entry, table) {
  const name = entry && entry.name;
  const path = `exports.${name}`;
  if (typeof name !== 'string' || name.length === 0) {
    throw new PkgError(
      'E_EXPORT_BINDING',
      'exports',
      'every manifest export must declare a non-empty "name"',
    );
  }
  if (!isRecord(table) || !Object.prototype.hasOwnProperty.call(table, name)) {
    throw new PkgError(
      'E_EXPORT_BINDING',
      path,
      `manifest export "${name}" must have a matching native symbol`,
    );
  }
  const sym = table[name];
  if (!isRecord(sym) || typeof sym.fn !== 'function') {
    throw new PkgError(
      'E_EXPORT_BINDING',
      path,
      `native symbol "${name}" must provide a callable "fn"`,
    );
  }
  if (!isRecord(sym) || sym.arity !== entry.arity) {
    throw new PkgError(
      'E_EXPORT_BINDING',
      path,
      `native symbol "${name}" must have arity ${entry.arity}`,
    );
  }
  if (!sameKinds(sym.argKinds, entry.argKinds)) {
    throw new PkgError(
      'E_EXPORT_BINDING',
      path,
      `native symbol "${name}" must have arg kinds [${(entry.argKinds || []).join(', ')}]`,
    );
  }
  if (
    typeof entry.returnKind === 'string' &&
    typeof sym.returnKind === 'string' &&
    sym.returnKind !== entry.returnKind
  ) {
    throw new PkgError(
      'E_EXPORT_BINDING',
      path,
      `native symbol "${name}" must have return kind "${entry.returnKind}"`,
    );
  }
  return sym.fn;
}

function bindExports(manifest, symbols) {
  const list = manifest && Array.isArray(manifest.exports) ? manifest.exports : [];
  const bound = {};
  for (const entry of list) {
    const fn = checkSymbol(entry, symbols);
    // defineProperty keeps even hostile names (e.g. "__proto__") as own
    // data properties instead of mutating the bound object's prototype.
    Object.defineProperty(bound, entry.name, {
      value: fn,
      enumerable: true,
      configurable: true,
      writable: true,
    });
  }
  return bound;
}

module.exports = { bindExports };
