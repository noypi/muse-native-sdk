'use strict';
// @muse/cli — `muse-native run <appDir>` builds the pipeline in memory then
// launches (stub launcher; device deploy is a later lane).
// Build chains in memory via the shared loader + lowering (`lib/ir.js`):
// missing dir / unreadable sources / no `.tsx` sources / empty lowering →
// stderr + exit 1 without launching. No `dist/` coupling: nothing is read
// from or written to `<appDir>/dist` here.
// Default target: host (documented here; device targets arrive later).
const fs = require('node:fs');
const path = require('node:path');
const ir = require('./ir.js');

// Default launch target. Kept as a named constant so the launch line and a
// future device-deploy lane share one value.
const DEFAULT_TARGET = 'host';

function runRun(appDir) {
  if (!appDir) {
    process.stderr.write('Error: missing <appDir>.\nUsage: muse-native run <appDir>\n');
    process.exitCode = 1;
    return;
  }
  const resolved = path.resolve(appDir);
  let stat = null;
  try {
    stat = fs.statSync(resolved);
  } catch (err) {
    process.stderr.write(`Error: directory '${appDir}' does not exist.\n`);
    process.exitCode = 1;
    return;
  }
  if (!stat.isDirectory()) {
    process.stderr.write(`Error: '${appDir}' is not a directory.\n`);
    process.exitCode = 1;
    return;
  }
  let sources;
  try {
    sources = ir.loadSources(resolved);
  } catch (err) {
    process.stderr.write(`Error: cannot read sources in '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  if (sources.length === 0) {
    process.stderr.write(`Error: no .tsx sources found in '${appDir}'.\n`);
    process.exitCode = 1;
    return;
  }
  try {
    const tree = ir.lowerToIR(sources);
    if (!tree || tree.version !== 1 || !Array.isArray(tree.roots) || tree.roots.length === 0) {
      throw new Error('lowering produced an empty tree');
    }
    // Full in-memory build pipeline: encoding must succeed too, even
    // though nothing is written to `dist/`.
    ir.encodeTree(tree);
  } catch (err) {
    process.stderr.write(`Error: cannot run '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(`launching ${appDir} (${DEFAULT_TARGET})\n`);
  process.exitCode = 0;
}

module.exports = runRun;
