'use strict';
// @muse/cli — `muse-native dev <appDir>` validates sources then announces a
// dev server and blocks until SIGINT.
// Validation reuses the same loader as build (`lib/ir.js`): missing dir /
// unreadable sources / no `.tsx` sources / empty lowering → stderr + exit 1
// before serving (no serve side-effect). No `dist/` coupling: nothing is
// read from or written to `<appDir>/dist` here.
// Serving is minimal in this lane: announce + block only. No socket is
// bound yet — later lanes own real serving (watch/hot-reload). The process
// stays alive on stdin/SIGINT and exits 0 on SIGINT.
// Default port: 8080 (documented here; overridable later via flag/env).
const fs = require('node:fs');
const path = require('node:path');
const ir = require('./ir.js');

// Default dev-server port. Overridable later (flag/env); kept as a named
// constant so the announcement and the future listener share one value.
const DEFAULT_PORT = 8080;

function runDev(appDir) {
  if (!appDir) {
    process.stderr.write('Error: missing <appDir>.\nUsage: muse-native dev <appDir>\n');
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
  } catch (err) {
    process.stderr.write(`Error: cannot serve '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(`serving ${appDir} at http://127.0.0.1:${DEFAULT_PORT}/\n`);
  // Announce + block: keep the event loop alive until SIGINT. No network
  // listen in this lane — later lanes own real serving.
  if (process.stdin && typeof process.stdin.resume === 'function') {
    try {
      process.stdin.resume();
    } catch (_) {
      // Best effort; the interval below still keeps the loop alive.
    }
  }
  const keepAlive = setInterval(() => {}, 1000);
  if (typeof keepAlive.unref === 'function') {
    // Stay referenced: ref explicitly in case a test harness ref policy
    // differs — the process must stay alive until SIGINT.
    keepAlive.ref();
  }
  process.on('SIGINT', () => {
    clearInterval(keepAlive);
    try {
      if (process.stdin && typeof process.stdin.pause === 'function') process.stdin.pause();
    } catch (_) {
      // Ignore cleanup errors on the way out.
    }
    process.exitCode = 0;
  });
}

module.exports = runDev;
