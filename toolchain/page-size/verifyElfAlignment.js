'use strict';
// T1 — ELF 16 KB alignment gate (AC-BUILD-016; required dep AC-BUILD-061 flag provenance).
// Verifies every shipped .so in a release tree has 16 KB-aligned LOAD segments.
// Hermetic marker-file convention (pinned by the unit-test contract):
//   <root>/lib/<abi>/<name>.so          shipped shared object
//   <root>/lib/<abi>/<name>.so.align    alignment marker: text "16384" means
//                                       16 KB-aligned, anything else means
//                                       misaligned; a missing marker counts as
//                                       misaligned (never silently accepted).

const fs = require('node:fs');
const path = require('node:path');

const ALIGNED_PAGE_SIZE = '16384';

function collectSharedObjects(dir, out) {
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
      collectSharedObjects(full, out);
    } else if (entry.isFile() && entry.name.endsWith('.so')) {
      out.push(full);
    }
  }
}

// verifyElfAlignment(rootDir: string): { ok: boolean, violations: string[] }
function verifyElfAlignment(rootDir) {
  const root = String(rootDir);

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
      violations: [`${root}: release root not found or not a directory`],
    };
  }

  const objects = [];
  collectSharedObjects(root, objects);

  const violations = [];
  for (const soPath of objects) {
    let marker = null;
    try {
      marker = fs.readFileSync(`${soPath}.align`, 'utf8').trim();
    } catch (err) {
      void err;
      marker = null;
    }
    if (marker !== ALIGNED_PAGE_SIZE) {
      const rel = path.relative(root, soPath);
      violations.push(
        `${rel}: LOAD segments are not 16 KB-aligned (expected marker '${ALIGNED_PAGE_SIZE}')`
      );
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { verifyElfAlignment };
