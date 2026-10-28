'use strict';
// FR-M3-004 / AC-M3-007 — manifest completeness build gate.
//
// Compares the component entries in muse.manifest.json against the
// `components` export keys of packages/native/index.js, in both directions:
//   - a manifest entry with no matching export is a missing component;
//   - an export with no manifest entry is an orphan implementation.
// Exits 0 on an exact match, 1 naming the offending entry otherwise.
//
// Testability (ASSUMPTION G1): input paths resolve from MANIFEST_PATH /
// COMPONENTS_PATH env vars when set, defaulting to the real manifest +
// index.js next to this script. Fixtures use tmp-dir copies so production
// files are never mutated.

const fs = require('node:fs');
const path = require('node:path');

const MANIFEST_PATH =
  process.env.MANIFEST_PATH || path.join(__dirname, '..', 'muse.manifest.json');
const COMPONENTS_PATH =
  process.env.COMPONENTS_PATH || path.join(__dirname, '..', 'index.js');

function fail(message) {
  console.error(`check-manifest: ${message}`);
  process.exit(1);
}

function loadManifest(manifestPath) {
  let raw;
  try {
    raw = fs.readFileSync(manifestPath, 'utf8');
  } catch (err) {
    fail(`cannot read manifest at ${manifestPath}: ${err.message}`);
  }
  let manifest;
  try {
    manifest = JSON.parse(raw);
  } catch (err) {
    fail(`manifest at ${manifestPath} is not valid JSON: ${err.message}`);
  }
  if (!manifest || !Array.isArray(manifest.components)) {
    fail(`manifest at ${manifestPath} must define a components array`);
  }
  return manifest;
}

function loadComponentNames(componentsPath, manifestNames) {
  let mod;
  try {
    mod = require(componentsPath);
  } catch (err) {
    // The components module must be loadable for the gate to verify an
    // exact match. Report the load error together with the manifest entries
    // that could not be verified so the missing/orphan entry is named.
    fail(
      `cannot load components module at ${componentsPath}: ${err.message}` +
        ` (unverified manifest entries: ${manifestNames.join(', ')})`,
    );
  }
  const candidates = [mod && mod.components, mod && mod.default && mod.default.components];
  for (const c of candidates) {
    if (c && typeof c === 'object') return Object.keys(c);
  }
  fail(`components module at ${componentsPath} must export a components record`);
  return [];
}

function main() {
  const manifest = loadManifest(MANIFEST_PATH);
  const manifestNames = manifest.components.map((entry) => entry && entry.name);
  const exportNames = loadComponentNames(COMPONENTS_PATH, manifestNames);

  const manifestSet = new Set(manifestNames);
  const exportSet = new Set(exportNames);

  const missing = manifestNames.filter((name) => !exportSet.has(name));
  const orphans = exportNames.filter((name) => !manifestSet.has(name));

  if (missing.length > 0) {
    fail(`missing component export for manifest entries: ${missing.join(', ')}`);
  }
  if (orphans.length > 0) {
    fail(`orphan manifest entry with no component export: ${orphans.join(', ')}`);
  }

  console.log(`check-manifest: OK — ${manifestNames.length} manifest entries match component exports`);
}

main();
