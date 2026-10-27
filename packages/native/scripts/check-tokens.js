'use strict';
// FR-M3-001 / AC-M3-002 — library token gate (T4, lib scope only).
// Fails the build when library component code contains a literal colour or
// dimension; token roles stay plain strings resolved by the theme layer.

const fs = require('node:fs');
const path = require('node:path');

const LIB_PATH = process.env.LIB_PATH || path.join(__dirname, '..');

const HEX_LITERAL = /#[0-9a-fA-F]{3,8}\b/;
const FN_COLOR_LITERAL = /\b(?:rgb|rgba|hsl|hsla)\s*\(/;
const DIMENSION_LITERAL = /\b\d+(?:\.\d+)?(?:px|dp|sp|pt)\b/;

const SKIP_DIRS = new Set(['test', 'scripts', 'node_modules', '.git']);

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
    } else if (entry.isFile() && entry.name.endsWith('.js')) {
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
  console.log(`check-tokens: OK — ${files.length} library files token-only`);
}

main();
