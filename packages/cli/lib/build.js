'use strict';
// @muse/cli — `muse-native build <appDir>` emits the UI IR + binary UI tree.
// Single source tree in, two artefacts out: `<appDir>/dist/ir.json` and
// `<appDir>/dist/tree.mui.bin`. Fails closed: missing dir / no `.tsx`
// sources is an error + exit 1 with no partial `dist/` writes (everything
// is validated in memory before anything is written).
const fs = require('node:fs');
const path = require('node:path');
const ir = require('./ir.js');

function runBuild(appDir) {
  if (!appDir) {
    process.stderr.write('Error: missing <appDir>.\nUsage: muse-native build <appDir>\n');
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
  // Validate before writing: lowering + encoding happen fully in memory,
  // so a failure here leaves `dist/` untouched.
  let tree;
  let bin;
  try {
    tree = ir.lowerToIR(sources);
    if (!tree || tree.version !== 1 || !Array.isArray(tree.roots) || tree.roots.length === 0) {
      throw new Error('lowering produced an empty tree');
    }
    bin = ir.encodeTree(tree);
    if (!bin || bin.length <= 4) throw new Error('encoding produced an empty blob');
  } catch (err) {
    process.stderr.write(`Error: cannot build '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  // Artefact containment: everything lands under <appDir>/dist, nowhere else.
  const distDir = path.join(resolved, 'dist');
  const irPath = path.join(distDir, 'ir.json');
  const binPath = path.join(distDir, 'tree.mui.bin');
  try {
    fs.mkdirSync(distDir, { recursive: true });
    fs.writeFileSync(irPath, JSON.stringify(tree, null, 2) + '\n', 'utf8');
    fs.writeFileSync(binPath, bin);
  } catch (err) {
    try {
      fs.rmSync(irPath, { force: true });
      fs.rmSync(binPath, { force: true });
    } catch (_) {
      // Best-effort cleanup; the error below is what matters.
    }
    process.stderr.write(`Error: cannot write artefacts for '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(`Built ${appDir}\nWrote ${irPath}\nWrote ${binPath}\n`);
  process.exitCode = 0;
}

module.exports = runBuild;
