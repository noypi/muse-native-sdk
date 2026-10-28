'use strict';

// Package loader entry point. Runs the fail-closed check pipeline in order —
// layout → integrity → ABI → exports → capabilities → no-render —
// short-circuiting with zero partial load. No `.so` is ever opened here:
// the integrity gate (`verifyIntegrity`) throws before any native open, and
// the later stages (ABI and beyond, owned by T3+) run only on bytes that
// already passed the hash gate.

const fs = require('node:fs');
const path = require('node:path');

const { PkgError } = require('./errors.js');
const { validateManifest } = require('./manifest.js');
const { validateLayout, CANONICAL_MANIFEST } = require('./layout.js');
const { verifyIntegrity } = require('./integrity.js');
const { checkAbiCoverage, checkEngineAbi } = require('./native.js');
const { bindExports } = require('./exports.js');
const { checkCapabilities } = require('./capabilities.js');
const { checkNoRender } = require('./no-render.js');

const INTEGRITY_PATH = 'integrity.json';

function toPosix(relPath) {
  return String(relPath).split(path.sep).join('/');
}

function listPackageFiles(pkgDir) {
  const names = [];
  const entries = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const abs = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(abs);
      } else if (entry.isFile()) {
        const rel = toPosix(path.relative(pkgDir, abs));
        names.push(rel);
        entries.push({ rel, abs });
      }
    }
  };
  walk(pkgDir);
  names.sort();
  entries.sort((a, b) => (a.rel < b.rel ? -1 : a.rel > b.rel ? 1 : 0));
  return { names, entries };
}

function readPackageDir(pkgDir) {
  let stat;
  try {
    stat = fs.statSync(pkgDir);
  } catch {
    throw new PkgError(
      'E_LAYOUT',
      String(pkgDir),
      'package directory must exist and be readable',
    );
  }
  if (!stat.isDirectory()) {
    throw new PkgError(
      'E_LAYOUT',
      String(pkgDir),
      'package directory must exist and be readable',
    );
  }
  return listPackageFiles(pkgDir);
}

function readJsonFile(abs, code, rel) {
  let text;
  try {
    text = fs.readFileSync(abs, 'utf8');
  } catch {
    throw new PkgError(code, rel, `"${rel}" must exist and be readable`);
  }
  try {
    return JSON.parse(text);
  } catch {
    throw new PkgError(code, rel, `"${rel}" must be valid JSON`);
  }
}

function loadPackage(pkgDir, opts) {
  const o = opts && typeof opts === 'object' ? opts : {};
  // Stage 0: fail closed when the directory itself is unusable.
  const { names, entries } = readPackageDir(pkgDir);
  const byName = new Map(entries.map((entry) => [entry.rel, entry.abs]));

  // Stage 1 (layout): manifest presence, allowed roots, entry form.
  // Runs before any hash comparison so off-layout dirs never reach verify.
  const manifestRel = byName.has(CANONICAL_MANIFEST) ? CANONICAL_MANIFEST : undefined;
  if (!manifestRel) {
    throw new PkgError(
      'E_LAYOUT',
      CANONICAL_MANIFEST,
      'package must contain muse-package.json at the root',
    );
  }
  const manifest = readJsonFile(byName.get(manifestRel), 'E_MANIFEST', manifestRel);
  validateManifest(manifest);
  validateLayout(names, manifest);

  // Stage 2 (integrity): every archive file must match integrity.json.
  // Throws E_INTEGRITY before any `.so` is opened. integrity.json cannot
  // cover itself, so it is the only file excluded from the hashed set.
  if (!byName.has(INTEGRITY_PATH)) {
    throw new PkgError(
      'E_INTEGRITY',
      INTEGRITY_PATH,
      `sha256 of every archive file listed in ${INTEGRITY_PATH}`,
    );
  }
  const integrity = readJsonFile(byName.get(INTEGRITY_PATH), 'E_INTEGRITY', INTEGRITY_PATH);
  const files = new Map();
  for (const { rel, abs } of entries) {
    if (rel === INTEGRITY_PATH) continue;
    files.set(rel, fs.readFileSync(abs));
  }
  verifyIntegrity(files, integrity);

  // Stages 3-4 (ABI): device coverage first, then the running engine hash.
  // Each throws (E_ABI_COVERAGE / E_ENGINE_ABI) with zero partial load.
  const deviceAbis = Array.isArray(o.deviceAbi)
    ? o.deviceAbi
    : typeof o.deviceAbi === 'string'
      ? [o.deviceAbi]
      : [];
  checkAbiCoverage(manifest, deviceAbis);
  checkEngineAbi(manifest, o.engineAbi);

  // Stage 5 (exports): bind only the manifest-declared surface against the
  // caller-staged symbol table. No `.so` is opened here; a declared export
  // with no staged entry throws E_EXPORT_BINDING.
  const stagedTable =
    o.symbols && typeof o.symbols === 'object' && !Array.isArray(o.symbols) ? o.symbols : {};
  bindExports(manifest, stagedTable);

  // Stage 6 (capabilities): every manifest capability must be granted.
  // Fail-closed default: opts carries no policyId, so use a named deny-by-default policy; empty-capability packages pass while any required capability still needs an explicit grant.
  const granted = Array.isArray(o.grantedCapabilities) ? o.grantedCapabilities : [];
  checkCapabilities(manifest, granted, typeof o.policyId === 'string' ? o.policyId : 'no-policy');

  // Stage 7 (no-render): declared capabilities plus staged native symbol
  // names only; never opens a native library.
  const stagedSymbols = [
    ...Object.keys(stagedTable),
    ...(Array.isArray(o.importedSymbols) ? o.importedSymbols : []),
  ];
  checkNoRender(manifest, stagedSymbols);

  return { dir: String(pkgDir), manifest, files: names };
}

module.exports = { loadPackage };
