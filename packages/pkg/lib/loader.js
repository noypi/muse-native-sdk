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
const { validateLayout, CANONICAL_MANIFEST, ALIAS_MANIFESTS } = require('./layout.js');
const { verifyIntegrity } = require('./integrity.js');

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
  void opts;
  // Stage 0: fail closed when the directory itself is unusable.
  const { names, entries } = readPackageDir(pkgDir);
  const byName = new Map(entries.map((entry) => [entry.rel, entry.abs]));

  // Stage 1 (layout): manifest presence, allowed roots, entry form.
  // Runs before any hash comparison so off-layout dirs never reach verify.
  const manifestRel =
    byName.has(CANONICAL_MANIFEST)
      ? CANONICAL_MANIFEST
      : ALIAS_MANIFESTS.find((alias) => byName.has(alias));
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

  // Stages 3+ (ABI → exports → capabilities → no-render) are owned by T3+ and
  // run only after the hash gate above has passed.
  return { dir: String(pkgDir), manifest, files: names };
}

module.exports = { loadPackage };
