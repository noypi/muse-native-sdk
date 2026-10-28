'use strict';

// JS-only package surface (FR-PKG-002, FR-PKG-004).
// Pure manifest-vs-list comparisons: never opens a native `.so`, never
// touches the network, a registry, or the filesystem.

const { PkgError } = require('./errors.js');

function isTraversalPath(value) {
  const normalized = String(value).replace(/\\/g, '/');
  if (normalized.length === 0) return true;
  if (normalized.startsWith('/')) return true;
  return normalized.split('/').some((segment) => segment === '..');
}

function hasNativeFiles(files) {
  const list = Array.isArray(files) ? files : [];
  return list.some(
    (file) => typeof file === 'string' && (file === 'native' || file.startsWith('native/')),
  );
}

function isJsOnly(manifest, files) {
  void manifest;
  return !hasNativeFiles(files);
}

function resolveEntry(manifest) {
  const entry =
    manifest && typeof manifest === 'object' && manifest.entry && typeof manifest.entry === 'object'
      ? manifest.entry
      : null;
  const declaredExports = manifest && Array.isArray(manifest.exports) ? manifest.exports : [];
  if (declaredExports.length > 0) {
    throw new PkgError(
      'E_JS_EXPORTS',
      'exports',
      'JS-only packages must declare an empty "exports" array',
    );
  }
  const form = entry ? entry.form : undefined;
  const main = entry ? entry.main : undefined;
  const types = entry ? entry.types : undefined;
  if (form !== 'prebuilt' && form !== 'source') {
    throw new PkgError(
      'E_ENTRY_FORM',
      typeof main === 'string' && main.length > 0 ? main : 'entry.form',
      'entry.form must be "prebuilt" or "source"',
    );
  }
  if (typeof main !== 'string' || main.length === 0 || isTraversalPath(main)) {
    throw new PkgError(
      'E_ENTRY_FORM',
      typeof main === 'string' && main.length > 0 ? main : 'entry.main',
      'entry.main must be a relative path inside the package',
    );
  }
  if (form === 'prebuilt') {
    if (typeof types !== 'string' || types.length === 0 || isTraversalPath(types)) {
      throw new PkgError(
        'E_ENTRY_FORM',
        'entry.types',
        'prebuilt entry.types must name the matching "src/index.d.ts"',
      );
    }
    return { kind: 'prebuilt', main, types };
  }
  if (typeof types !== 'string' || (types.length > 0 && isTraversalPath(types))) {
    throw new PkgError(
      'E_ENTRY_FORM',
      'entry.types',
      'source entry.types must be a path string inside the package',
    );
  }
  return { kind: 'source', main, types };
}

function badSpecifier(specifier, expected) {
  const path = typeof specifier === 'string' && specifier.length > 0 ? specifier : 'specifier';
  return new PkgError('E_MANIFEST', path, expected);
}

async function importPackage(specifier) {
  if (typeof specifier !== 'string' || specifier.length === 0) {
    throw badSpecifier(specifier, 'package specifier must be a non-empty string');
  }
  const normalized = specifier.replace(/\\/g, '/');
  const traversal =
    normalized.startsWith('/') ||
    normalized.startsWith('.') ||
    normalized.split('/').some((segment) => segment === '..');
  if (traversal) {
    throw badSpecifier(specifier, 'package specifier must be a bare package name inside the registry');
  }
  // No registry is contacted here: unknown specifiers fail closed with a
  // named error rather than resolving to anything.
  throw badSpecifier(specifier, `unknown package specifier "${specifier}"`);
}

module.exports = { isJsOnly, resolveEntry, importPackage };
