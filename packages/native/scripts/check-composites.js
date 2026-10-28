'use strict';
// FR-M3-003 / AC-M3-005 — tier-3 import/native-registration build gate (T2).
//
// Fails the build when a tier-3 composite source imports a package outside
// `@muse/core` / `@muse/native` (including relative imports, which leave the
// package closure), or registers a native primitive (`registerNative` /
// `registerPrimitive` / `NativeModules.`). Scans `composites/*.ts(x)` plus
// the tier-3 section of `packages/native/index.js` (from the tier-3 marker
// comment onward, mirroring the `Mirrors composites/*.tsx` contract).
// Exits 1 naming file:line on violation, 0 with an OK line otherwise.
//
// Testability: input paths resolve from COMPOSITES_DIR / COMPONENTS_PATH
// env vars when set, defaulting to the real composites dir + index.js next
// to this script. Fixtures use tmp-dir copies so production files are never
// mutated.

const fs = require('node:fs');
const path = require('node:path');

const COMPOSITES_DIR =
  process.env.COMPOSITES_DIR || path.join(__dirname, '..', 'composites');
const COMPONENTS_PATH =
  process.env.COMPONENTS_PATH || path.join(__dirname, '..', 'index.js');

const ALLOWED = new Set(['@muse/core', '@muse/native']);
// Matches `import 'spec'`, `import x from 'spec'`, `require('spec')`, and a
// bare continuation `from 'spec'` (multiline import whose specifier sits on
// its own line).
const SPEC_RE =
  /(?:import|require)\s*\(?\s*['"]([^'"]+)['"]|\bfrom\s+['"]([^'"]+)['"]/g;
const NATIVE_RE = /registerNative|registerPrimitive|NativeModules\s*\./;
const TIER3_MARKER = /tier-3 composites/;

function fail(violations) {
  for (const v of violations) console.error(`check-composites: ${v}`);
  process.exit(1);
}

function checkLine(source, file, lineNo, violations) {
  for (const m of source.matchAll(SPEC_RE)) {
    const spec = m[1] !== undefined ? m[1] : m[2];
    if (spec.startsWith('node:')) continue;
    if (spec.startsWith('.') || spec.startsWith('/')) {
      violations.push(`relative import "${spec}" at ${file}:${lineNo}`);
    } else if (!ALLOWED.has(spec)) {
      violations.push(`forbidden import "${spec}" at ${file}:${lineNo}`);
    }
  }
  if (NATIVE_RE.test(source)) {
    violations.push(`native primitive registration at ${file}:${lineNo}`);
  }
}

function checkSourceFile(file, violations) {
  let source;
  try {
    source = fs.readFileSync(file, 'utf8');
  } catch (err) {
    violations.push(`cannot read source at ${file}: ${err.message}`);
    return;
  }
  const lines = source.split('\n');
  for (let i = 0; i < lines.length; i += 1) {
    checkLine(lines[i], file, i + 1, violations);
  }
}

function main() {
  const violations = [];
  let scanned = 0;

  let entries;
  try {
    entries = fs.readdirSync(COMPOSITES_DIR, { withFileTypes: true });
  } catch (err) {
    fail([`cannot read composites dir at ${COMPOSITES_DIR}: ${err.message}`]);
  }
  const compositeFiles = entries
    .filter((e) => e.isFile() && (e.name.endsWith('.tsx') || e.name.endsWith('.ts')))
    .map((e) => path.join(COMPOSITES_DIR, e.name))
    .sort();
  if (compositeFiles.length === 0) {
    fail([`no composite sources found in ${COMPOSITES_DIR}`]);
  }
  for (const file of compositeFiles) {
    checkSourceFile(file, violations);
    scanned += 1;
  }

  let indexSource;
  try {
    indexSource = fs.readFileSync(COMPONENTS_PATH, 'utf8');
  } catch (err) {
    fail([`cannot read components module at ${COMPONENTS_PATH}: ${err.message}`]);
  }
  const indexLines = indexSource.split('\n');
  const markerAt = indexLines.findIndex((line) => TIER3_MARKER.test(line));
  if (markerAt === -1) {
    fail([`tier-3 section marker missing in ${COMPONENTS_PATH}`]);
  }
  // The tier-3 section runs from the marker through the closing `};` of the
  // `tier3` record. Scoping (rather than scanning to end-of-file) keeps the
  // pre-existing tail re-export below the record out of the closure check.
  const recordAt = indexLines.findIndex(
    (line, i) => i >= markerAt && /^const tier3 = \{$/.test(line),
  );
  if (recordAt === -1) {
    fail([`tier-3 record missing in ${COMPONENTS_PATH}`]);
  }
  const endAt = indexLines.findIndex(
    (line, i) => i > recordAt && /^};$/.test(line),
  );
  if (endAt === -1) {
    fail([`tier-3 record unterminated in ${COMPONENTS_PATH}`]);
  }
  for (let i = markerAt; i <= endAt; i += 1) {
    checkLine(indexLines[i], COMPONENTS_PATH, i + 1, violations);
  }
  scanned += 1;

  if (violations.length > 0) fail(violations);
  console.log(
    `check-composites: OK — ${scanned} tier-3 sources import-closed, no native registration`,
  );
}

if (require.main === module) {
  main();
}

module.exports = { checkLine };
