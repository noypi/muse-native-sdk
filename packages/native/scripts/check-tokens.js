'use strict';
// FR-M3-005 / AC-M3-009 — library token gate (T4, lib scope only) with the
// application-literal advisory boundary (T7).
// Fails the build when library component code contains a literal colour or
// dimension; token roles stay plain strings resolved by the theme layer.
// CONFLICT-M3-005 scope assignment: library literals fail the gate, while the
// same literal in application code only warns via the lintAppLiteral advisory
// (FR-TSX-010 / AC-TSX-027 boundary) — application literals warn (advisory)
// instead of failing, so the app path never exits non-zero.

const fs = require('node:fs');
const path = require('node:path');

const LIB_PATH = process.env.LIB_PATH || path.join(__dirname, '..');

const HEX_LITERAL = /#[0-9a-fA-F]{3,8}\b/;
const FN_COLOR_LITERAL = /\b(?:rgb|rgba|hsl|hsla)\s*\(/;
const DIMENSION_LITERAL = /\b\d+(?:\.\d+)?(?:px|dp|sp|pt)\b/;

const SKIP_DIRS = new Set(['test', 'scripts', 'node_modules', '.git']);

// FR-LAYOUT-016 T3 / AC-LAYOUT-052+053 — per-component token-table gate.
// Fails when (a) a matrix/registry component has no token table, or (b)
// resolveTokens(component, mode) output differs from the tabled roles for
// light and dark. Runs after the literal detection above and never weakens
// it: literal violations still exit 1 first.
const CHECKOUT_ROOT = path.join(__dirname, '..', '..', '..');
const MATRIX_JSON = path.join(CHECKOUT_ROOT, 'engine', 'layout', 'm3_matrix.json');
const REGISTRY_JS = path.join(CHECKOUT_ROOT, 'engine', 'layout', 'm3_registry.js');
const THEME_JS = path.join(__dirname, '..', 'theme.js');

const TOKEN_FACETS = ['container', 'label', 'state', 'shape', 'elevation', 'typescale'];
const TOKEN_MODES = ['light', 'dark'];
const TOKEN_SOURCES = new Set(['component', 'role', 'theme', 'default']);
const STATE_ROLE = /^md\.comp\.[a-z0-9-]+\.state\.[a-z0-9-]+$/;

function scalarRolePattern(facet) {
  return new RegExp(`^md\\.comp\\.[a-z0-9-]+\\.${facet}$`);
}

function sameRoleValue(a, b) {
  return JSON.stringify(a) === JSON.stringify(b);
}

function isWellFormedTable(table) {
  for (const facet of TOKEN_FACETS) {
    if (!Object.hasOwn(table, facet)) return false;
  }
  for (const facet of ['container', 'label', 'shape', 'elevation', 'typescale']) {
    if (typeof table[facet] !== 'string') return false;
    // Per-component roles only (no generic fallback, no ad-hoc suffixes):
    // each scalar role is exactly md.comp.<slug>.<facet>.
    if (!scalarRolePattern(facet).test(table[facet])) return false;
  }
  if (!Array.isArray(table.state)) return false;
  for (const role of table.state) {
    if (typeof role !== 'string' || !STATE_ROLE.test(role)) return false;
  }
  if (table.sources === null || typeof table.sources !== 'object') return false;
  for (const facet of TOKEN_FACETS) {
    if (!Object.hasOwn(table.sources, facet)) return false;
    if (!TOKEN_SOURCES.has(table.sources[facet])) return false;
  }
  return true;
}

function registryComponentNames() {
  let registry = null;
  try {
    // eslint-disable-next-line global-require
    registry = require(REGISTRY_JS);
  } catch (err) {
    registry = null;
  }
  const names = [];
  if (registry !== null) {
    for (const key of ['NATIVE_COMPONENTS', 'COMPOSITE_COMPONENTS']) {
      if (Array.isArray(registry[key])) {
        for (const name of registry[key]) {
          if (typeof name === 'string' && name !== '') names.push(name);
        }
      }
    }
    if (names.length > 0) return names;
  }
  // Registry lists are module-private: fall back to reading the
  // NATIVE_COMPONENTS / COMPOSITE_COMPONENTS literals from its source.
  try {
    const source = fs.readFileSync(REGISTRY_JS, 'utf8');
    for (const list of ['NATIVE_COMPONENTS', 'COMPOSITE_COMPONENTS']) {
      const block = source.match(new RegExp(`const ${list} = \\[([\\s\\S]*?)\\];`));
      if (!block) continue;
      for (const match of block[1].matchAll(/'([^']+)'/g)) names.push(match[1]);
    }
  } catch (err) {
    // Matrix remains authoritative; a missing registry source just means no
    // extra names beyond the matrix.
  }
  return names;
}

// Returns a list of { component, detail } gate failures (empty when clean).
function checkTokenTables() {
  const failures = [];
  let matrix;
  try {
    matrix = JSON.parse(fs.readFileSync(MATRIX_JSON, 'utf8'));
  } catch (err) {
    throw new Error(`cannot read token matrix at ${MATRIX_JSON}: ${err.message}`);
  }
  const matrixNames = Array.isArray(matrix.components)
    ? matrix.components.map((c) => c && c.name).filter((n) => typeof n === 'string')
    : [];
  const universe = new Set([...matrixNames, ...registryComponentNames()]);
  // eslint-disable-next-line global-require
  const registry = require(REGISTRY_JS);
  // eslint-disable-next-line global-require
  const theme = require(THEME_JS);
  if (
    typeof registry.getComponentTokenTable !== 'function' ||
    typeof theme.resolveTokens !== 'function'
  ) {
    throw new Error('token table gate requires getComponentTokenTable and resolveTokens');
  }
  for (const name of universe) {
    let table;
    try {
      table = registry.getComponentTokenTable(name);
    } catch (err) {
      failures.push({ component: name, detail: 'missing token table' });
      continue;
    }
    if (!isWellFormedTable(table)) {
      failures.push({ component: name, detail: 'tabled roles are malformed' });
      continue;
    }
    for (const mode of TOKEN_MODES) {
      let resolved;
      try {
        resolved = theme.resolveTokens(name, mode);
      } catch (err) {
        failures.push({ component: name, detail: `resolveTokens threw for mode "${mode}"` });
        break;
      }
      for (const facet of TOKEN_FACETS) {
        if (!sameRoleValue(resolved[facet], table[facet])) {
          failures.push({
            component: name,
            detail: `resolveTokens diverged from the tabled role "${facet}" for mode "${mode}"`,
          });
          break;
        }
      }
      if (failures.length > 0 && failures[failures.length - 1].component === name) break;
    }
  }
  return failures;
}

function hasLiteral(line) {
  return (
    HEX_LITERAL.test(line) || FN_COLOR_LITERAL.test(line) || DIMENSION_LITERAL.test(line)
  );
}

function collectJsFiles(root, into = []) {
  let entries;
  try {
    entries = fs.readdirSync(root, { withFileTypes: true });
  } catch (err) {
    console.error(`check-tokens: cannot read library path at ${root}: ${err.message}`);
    process.exit(1);
  }
  for (const entry of entries) {
    const full = path.join(root, entry.name);
    if (entry.isDirectory()) {
      if (SKIP_DIRS.has(entry.name)) continue;
      collectJsFiles(full, into);
    } else if (
      entry.isFile() &&
      (entry.name.endsWith('.js') || entry.name.endsWith('.tsx') || entry.name.endsWith('.ts'))
    ) {
      into.push(full);
    }
  }
  return into;
}

function main() {
  const args = process.argv.slice(2);
  let scope = null;
  for (let i = 0; i < args.length; i += 1) {
    if (args[i] === '--scope' && i + 1 < args.length) scope = args[i + 1];
    else if (args[i].startsWith('--scope=')) scope = args[i].slice('--scope='.length);
  }
  if (scope !== 'lib') {
    console.error('check-tokens: usage: node scripts/check-tokens.js --scope lib');
    process.exit(1);
  }
  const stat = fs.statSync(LIB_PATH, { throwIfNoEntry: false });
  const files =
    stat && stat.isFile() ? [LIB_PATH] : collectJsFiles(LIB_PATH);
  const violations = [];
  for (const file of files) {
    const source = fs.readFileSync(file, 'utf8');
    const lines = source.split('\n');
    for (let i = 0; i < lines.length; i += 1) {
      if (hasLiteral(lines[i])) violations.push(`${file}:${i + 1}`);
    }
  }
  if (violations.length > 0) {
    for (const v of violations) console.error(`check-tokens: literal token value at ${v}`);
    process.exit(1);
  }
  let tableFailures;
  try {
    tableFailures = checkTokenTables();
  } catch (err) {
    console.error(`check-tokens: token table gate error: ${err.message}`);
    process.exit(1);
  }
  if (tableFailures.length > 0) {
    for (const f of tableFailures) {
      console.error(`check-tokens: token table gate failed for component "${f.component}" — ${f.detail}`);
    }
    process.exit(1);
  }
  console.log(`check-tokens: OK — ${files.length} library files token-only`);
}

// FR-TSX-010 / AC-TSX-027 boundary (CONFLICT-M3-005): application-literal
// advisory. Always returns an advisory record and never throws or exits
// non-zero, so application code warns instead of failing the build.
// Defined here beside the lib gate so the package entry can re-export it.
function lintAppLiteral(file, line) {
  return {
    code: 'CAPO0012',
    severity: 'advisory',
    message: `app literal advisory at ${file}:${line} — prefer an M3 token role`,
  };
}

if (require.main === module) {
  main();
}

module.exports = { lintAppLiteral };
