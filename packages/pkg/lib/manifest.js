'use strict';

const { PkgError } = require('./errors.js');

const MANIFEST_PATH = 'muse-package.json';
const ENTRY_FORMS = ['prebuilt', 'source'];

function isObject(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function fail(code, path, expected) {
  throw new PkgError(code, path, expected);
}

function checkName(manifest) {
  if (typeof manifest.name !== 'string' || manifest.name.length === 0) {
    fail('E_MANIFEST', MANIFEST_PATH, 'manifest must declare a non-empty "name"');
  }
}

function checkVersion(manifest) {
  if (typeof manifest.version !== 'string' || manifest.version.length === 0) {
    fail('E_MANIFEST', 'version', 'manifest must declare a non-empty "version"');
  }
}

function checkMuseApi(manifest) {
  if (typeof manifest.museApi !== 'string' || manifest.museApi.length === 0) {
    fail('E_MANIFEST', 'museApi', 'manifest must declare a non-empty "museApi"');
  }
}

function checkEntry(manifest) {
  if (!isObject(manifest.entry)) {
    fail('E_MANIFEST', 'entry', 'manifest must declare an "entry" object');
  }
  const { form, main, types } = manifest.entry;
  if (form !== 'prebuilt' && form !== 'source') {
    fail('E_MANIFEST', 'entry.form', 'entry.form must be "prebuilt" or "source"');
  }
  if (typeof main !== 'string' || main.length === 0) {
    fail('E_MANIFEST', 'entry.main', 'entry.main must be a non-empty path');
  }
  if (typeof types !== 'string') {
    fail('E_MANIFEST', 'entry.types', 'entry.types must be a path string');
  }
  if (form === 'prebuilt' && types.length === 0) {
    fail('E_MANIFEST', 'entry.types', 'prebuilt entry.types must name "src/index.d.ts"');
  }
}

function checkAbis(manifest) {
  if (!Array.isArray(manifest.abis) || manifest.abis.length === 0) {
    fail('E_MANIFEST', 'abis', 'manifest must declare a non-empty "abis" array');
  }
  for (const abi of manifest.abis) {
    if (typeof abi !== 'string' || abi.length === 0) {
      fail('E_MANIFEST', 'abis', 'every entry of "abis" must be a non-empty string');
    }
  }
}

function checkEngineAbi(manifest) {
  if (typeof manifest.engineAbi !== 'string' || manifest.engineAbi.length === 0) {
    fail('E_MANIFEST', 'engineAbi', 'manifest must declare a non-empty "engineAbi"');
  }
}

function checkExports(manifest) {
  if (!Array.isArray(manifest.exports)) {
    fail('E_MANIFEST', 'exports', 'manifest must declare an "exports" array');
  }
}

function checkCapabilities(manifest) {
  if (!Array.isArray(manifest.capabilities)) {
    fail('E_MANIFEST', 'capabilities', 'manifest must declare a "capabilities" array');
  }
  for (const cap of manifest.capabilities) {
    if (typeof cap !== 'string') {
      fail('E_MANIFEST', 'capabilities', 'every entry of "capabilities" must be a string');
    }
  }
}

function validateManifest(manifest) {
  if (!isObject(manifest)) {
    fail('E_MANIFEST', MANIFEST_PATH, 'manifest must be an object parsed from muse-package.json');
  }
  checkName(manifest);
  checkVersion(manifest);
  checkMuseApi(manifest);
  checkEntry(manifest);
  checkAbis(manifest);
  checkEngineAbi(manifest);
  checkExports(manifest);
  checkCapabilities(manifest);
  return {
    name: manifest.name,
    version: manifest.version,
    museApi: manifest.museApi,
    entry: {
      form: manifest.entry.form,
      main: manifest.entry.main,
      types: manifest.entry.types,
    },
    abis: manifest.abis.slice(),
    engineAbi: manifest.engineAbi,
    exports: manifest.exports.slice(),
    capabilities: manifest.capabilities.slice(),
  };
}

module.exports = { validateManifest, ENTRY_FORMS, MANIFEST_PATH };
