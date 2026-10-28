'use strict';
// T11a — FR-DX-005 / AC-DX-021 / TC-DX-021.
// Compares committed declaration files against a fresh T10 regeneration.

const { generateDeclarations } = require('./generate');

function fail(code, cause, action) {
  const err = new Error(cause);
  err.code = code;
  err.cause = cause;
  err.action = action;
  throw err;
}

function checkInSync(manifest, committedFiles) {
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest)) {
    fail(
      'E_CODEGEN_BAD_MANIFEST',
      'DX-005: manifest must be an object with a components array',
      'Pass a manifest envelope shaped { components: [{ name, props?, methods?, events?, enums? }] } to checkInSync.',
    );
  }
  if (!committedFiles || typeof committedFiles !== 'object' || Array.isArray(committedFiles)) {
    fail(
      'E_CODEGEN_BAD_COMMITTED',
      'DX-005: committedFiles must be an object mapping module name to file text',
      'Pass the committed declaration map shaped { [moduleName]: string } to checkInSync.',
    );
  }
  const fresh = generateDeclarations(manifest).files;
  const stale = [];
  for (const name of Object.keys(fresh)) {
    if (!Object.prototype.hasOwnProperty.call(committedFiles, name) || committedFiles[name] !== fresh[name]) {
      stale.push(name);
    }
  }
  for (const name of Object.keys(committedFiles)) {
    if (!Object.prototype.hasOwnProperty.call(fresh, name)) {
      stale.push(name);
    }
  }
  return { inSync: stale.length === 0, stale };
}

module.exports = { checkInSync };
