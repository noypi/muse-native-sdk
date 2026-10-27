'use strict';
// T6 — unstripped-artifact retention gate (NFR-BUILD-016 / AC-BUILD-058).
// Requires every shipped stripped .so to have a retained unstripped
// counterpart plus its strip map; a stripped-only layout fails naming the
// orphan artifact.
//
// Dist layout (pinned by the unit-test contract):
//   <dist>/lib/<abi>/libmuse.so                  shipped stripped artifact
//   <dist>/symbols/unstripped/<abi>/libmuse.so    retained unstripped counterpart
//   <dist>/symbols/STRIPMAP.md                    strip map: one
//                                                `<stripped> -> <unstripped>`
//                                                mapping line per shipped .so

const fs = require('node:fs');
const path = require('node:path');

const STRIPMAP_REL = path.join('symbols', 'STRIPMAP.md');

function toPosix(rel) {
  return rel.split(path.sep).join('/');
}

function readText(filePath) {
  try {
    return { text: fs.readFileSync(filePath, 'utf8') };
  } catch (err) {
    return { error: err };
  }
}

function listAbis(dir) {
  try {
    return fs
      .readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isDirectory())
      .map((entry) => entry.name);
  } catch (err) {
    void err;
    return [];
  }
}

// Collect shipped .so files under <dist>/lib as paths relative to distDir,
// e.g. `lib/<abi>/libmuse.so`. Walks recursively so nested layouts work.
function listShippedRelPaths(distDir) {
  const libDir = path.join(distDir, 'lib');
  const found = [];
  function walk(dir) {
    let entries;
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch (err) {
      void err;
      return;
    }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        walk(full);
      } else if (entry.isFile() && entry.name.endsWith('.so')) {
        found.push(path.relative(distDir, full));
      }
    }
  }
  walk(libDir);
  return found.sort();
}

// Map a shipped `lib/<abi>/<rest>` path to its retained counterpart
// `symbols/unstripped/<abi>/<rest>`. Non-lib paths fall back to mirroring
// under `symbols/unstripped/`.
function unstrippedRelFor(shippedRel) {
  const parts = shippedRel.split(path.sep);
  if (parts.length >= 3 && parts[0] === 'lib') {
    return path.join('symbols', 'unstripped', ...parts.slice(1));
  }
  return path.join('symbols', 'unstripped', ...parts);
}

// verifySymbolRetention(distDir: string): { ok: boolean, violations: string[] }
function verifySymbolRetention(distDir) {
  const violations = [];
  const root = String(distDir);

  let rootStat = null;
  try {
    rootStat = fs.statSync(root);
  } catch (err) {
    void err;
    rootStat = null;
  }
  if (!rootStat || !rootStat.isDirectory()) {
    return {
      ok: false,
      violations: [`${root}: dist directory not found or not a directory`],
    };
  }

  const shipped = listShippedRelPaths(root);
  if (shipped.length === 0) {
    violations.push(
      `lib/<abi>/libmuse.so: no shipped stripped artifact under '${path.join(root, 'lib')}'`
    );
  }

  // Every shipped stripped .so needs its retained unstripped counterpart.
  for (const shippedRel of shipped) {
    const unstrippedRel = unstrippedRelFor(shippedRel);
    const unstrippedFull = path.join(root, unstrippedRel);
    const content = readText(unstrippedFull);
    if (content.error) {
      violations.push(
        `${toPosix(unstrippedRel)}: missing retained unstripped artifact for shipped '${toPosix(shippedRel)}'`
      );
    }
  }

  // The strip map is required evidence — its absence fails the gate even
  // when both .so markers are present.
  const mapFull = path.join(root, STRIPMAP_REL);
  const mapRead = readText(mapFull);
  if (mapRead.error) {
    violations.push(
      `${toPosix(STRIPMAP_REL)}: missing strip map '${mapFull}' mapping stripped -> unstripped artifacts`
    );
  } else {
    const mapText = mapRead.text;
    for (const shippedRel of shipped) {
      const unstrippedRel = unstrippedRelFor(shippedRel);
      const shippedPosix = toPosix(shippedRel);
      const unstrippedPosix = toPosix(unstrippedRel);
      const hasEntry = mapText
        .split('\n')
        .some((line) => line.includes(shippedPosix) && line.includes(unstrippedPosix) && line.includes('->'));
      if (!hasEntry) {
        violations.push(
          `${toPosix(STRIPMAP_REL)}: missing strip-map entry '${shippedPosix} -> ${unstrippedPosix}' for shipped '${shippedPosix}'`
        );
      }
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { verifySymbolRetention };
