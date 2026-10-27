'use strict';
// @muse/cli — `muse-native init <dir>` project scaffolding.
// Copies the template tree (muse-native.yaml, src/App.tsx, package.json)
// into <dir>. Fails closed: missing operand is usage + exit 2, an
// existing non-empty dir is an error + exit 1 with no writes.
const fs = require('node:fs');
const path = require('node:path');

const TEMPLATES_DIR = path.join(__dirname, '..', 'templates');
const TEMPLATE_FILES = ['muse-native.yaml', path.join('src', 'App.tsx'), 'package.json'];

function printInitUsage() {
  process.stderr.write('Usage: muse-native init <dir>\n');
}

function runInit(dir) {
  if (!dir) {
    printInitUsage();
    process.exitCode = 2;
    return;
  }
  let entries = null;
  try {
    const stat = fs.statSync(dir);
    if (!stat.isDirectory()) {
      process.stderr.write(`Error: '${dir}' exists and is not a directory.\n`);
      process.exitCode = 1;
      return;
    }
    entries = fs.readdirSync(dir);
  } catch (err) {
    if (!err || err.code !== 'ENOENT') {
      process.stderr.write(`Error: cannot init '${dir}': ${err && err.message ? err.message : err}\n`);
      process.exitCode = 1;
      return;
    }
    entries = null;
  }
  if (entries !== null && entries.length > 0) {
    process.stderr.write(`Error: directory '${dir}' already exists and is not empty.\n`);
    process.exitCode = 1;
    return;
  }
  try {
    fs.mkdirSync(dir, { recursive: true });
    for (const rel of TEMPLATE_FILES) {
      const src = path.join(TEMPLATES_DIR, rel);
      const dest = path.join(dir, rel);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.copyFileSync(src, dest);
    }
  } catch (err) {
    process.stderr.write(`Error: cannot init '${dir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(`Scaffolded new muse-native project in ${dir}\n`);
  process.exitCode = 0;
}

module.exports = runInit;
