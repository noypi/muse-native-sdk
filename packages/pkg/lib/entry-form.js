'use strict';

// Entry-form checks for `src/` against the manifest `entry.form`.
// `validateLayout` here enforces the full T1 layout contract (traversal,
// manifest presence, allowed roots) plus the entry-form half, by delegating
// to `lib/layout.js`, so both `require('../lib/layout.js')` and
// `require('../lib/entry-form.js')` resolve the pinned T1 interface.

const { PkgError } = require('./errors.js');
const layout = require('./layout.js');

const PREBUILT_MAIN = 'src/index.js';
const PREBUILT_TYPES = 'src/index.d.ts';
const SOURCE_MAINS = ['src/index.tsx', 'src/index.ts'];

function checkEntryForm(files, entry) {
  const set = new Set(files);
  const form = entry && entry.form;
  if (form !== 'prebuilt' && form !== 'source') {
    throw new PkgError('E_MANIFEST', 'entry.form', 'entry.form must be "prebuilt" or "source"');
  }
  if (form === 'prebuilt') {
    const main =
      entry && typeof entry.main === 'string' && entry.main.length > 0 ? entry.main : PREBUILT_MAIN;
    const types =
      entry && typeof entry.types === 'string' && entry.types.length > 0
        ? entry.types
        : PREBUILT_TYPES;
    if (!set.has(main)) {
      throw new PkgError('E_ENTRY_FORM', main, `prebuilt entry requires "${main}" in src/`);
    }
    if (!set.has(types)) {
      throw new PkgError('E_ENTRY_FORM', types, `prebuilt entry requires "${types}" in src/`);
    }
    return;
  }
  const main =
    entry && typeof entry.main === 'string' && entry.main.length > 0 ? entry.main : SOURCE_MAINS[0];
  if (set.has(main)) return;
  throw new PkgError('E_ENTRY_FORM', main, `source entry requires "${main}" in src/`);
}

function validateLayout(files, manifest) {
  layout.validateLayout(files, manifest);
}

module.exports = { validateLayout, checkEntryForm, PREBUILT_MAIN, PREBUILT_TYPES, SOURCE_MAINS };
