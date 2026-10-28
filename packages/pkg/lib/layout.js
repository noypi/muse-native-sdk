'use strict';

const { PkgError } = require('./errors.js');

const CANONICAL_MANIFEST = 'muse-package.json';
const ALIAS_MANIFESTS = ['capo-package.json'];
const ROOT_FILES = ['muse-package.json', 'capo-package.json', 'integrity.json'];
const ROOT_DIRS = ['src/', 'native/', 'assets/'];

function isTraversal(file) {
  const normalized = String(file).replace(/\\/g, '/');
  if (normalized.length === 0) return true;
  if (normalized.startsWith('/')) return true;
  const segments = normalized.split('/');
  return segments.some((segment) => segment === '..');
}

function checkTraversal(files) {
  for (const file of files) {
    if (typeof file !== 'string' || file.length === 0 || isTraversal(file)) {
      const bad = typeof file === 'string' ? file : String(file);
      throw new PkgError(
        'E_LAYOUT',
        bad,
        'package paths must be relative and stay inside the package',
      );
    }
  }
}

function hasManifest(files) {
  return files.some((file) => file === CANONICAL_MANIFEST || ALIAS_MANIFESTS.includes(file));
}

function checkRoots(files) {
  for (const file of files) {
    if (ROOT_FILES.includes(file)) continue;
    const underRoot = ROOT_DIRS.some((dir) => file.startsWith(dir) && file.length > dir.length);
    if (!underRoot) {
      throw new PkgError(
        'E_LAYOUT',
        file,
        'package layout allows only muse-package.json, src/, native/, assets/, integrity.json',
      );
    }
  }
}

function entryOf(manifest) {
  if (manifest && typeof manifest === 'object' && manifest.entry && typeof manifest.entry === 'object') {
    return manifest.entry;
  }
  return { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' };
}

function checkEntryForm(files, manifest) {
  const entry = entryOf(manifest);
  const set = new Set(files);
  if (entry.form !== 'prebuilt' && entry.form !== 'source') {
    throw new PkgError(
      'E_MANIFEST',
      'entry.form',
      'entry.form must be "prebuilt" or "source"',
    );
  }
  if (entry.form === 'prebuilt') {
    const main = typeof entry.main === 'string' && entry.main.length > 0 ? entry.main : 'src/index.js';
    const types =
      typeof entry.types === 'string' && entry.types.length > 0 ? entry.types : 'src/index.d.ts';
    if (!set.has(main)) {
      throw new PkgError('E_ENTRY_FORM', main, `prebuilt entry requires "${main}" in src/`);
    }
    if (!set.has(types)) {
      throw new PkgError('E_ENTRY_FORM', types, `prebuilt entry requires "${types}" in src/`);
    }
    return;
  }
  const main = typeof entry.main === 'string' && entry.main.length > 0 ? entry.main : 'src/index.tsx';
  if (set.has(main)) return;
  // Accept a .ts sibling when the manifest names a .tsx entry (and vice versa).
  const sibling =
    main.endsWith('.tsx') ? main.slice(0, -4) + '.ts' : main.endsWith('.ts') ? main.slice(0, -3) + '.tsx' : null;
  if (sibling && set.has(sibling)) return;
  throw new PkgError('E_ENTRY_FORM', main, `source entry requires "${main}" in src/`);
}

function validateLayout(files, manifest) {
  if (!Array.isArray(files)) {
    throw new PkgError('E_LAYOUT', CANONICAL_MANIFEST, 'package file list must be an array of paths');
  }
  checkTraversal(files);
  if (!hasManifest(files)) {
    throw new PkgError(
      'E_LAYOUT',
      CANONICAL_MANIFEST,
      'package must contain muse-package.json at the root',
    );
  }
  checkRoots(files);
  checkEntryForm(files, manifest);
}

module.exports = {
  validateLayout,
  checkEntryForm,
  CANONICAL_MANIFEST,
  ALIAS_MANIFESTS,
  ROOT_FILES,
  ROOT_DIRS,
};
