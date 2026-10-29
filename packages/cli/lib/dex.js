'use strict';
// @muse/cli — classes.dex compiler (FR-PACKAGING-006, AC-PACKAGING-015).
// Drives the SDK toolchain (`javac` against `android.jar`, then `d8`) over
// the app shell java sources and returns the dex bytes. Pure fail-closed
// lane: every input (source list, per-source path, android.jar,
// buildToolsDir) is validated in memory before any tool is spawned, and a
// toolchain failure surfaces as `Error('dex compile failed: ...')` naming
// the failing tool and the source file. Never touches `dist/` — all
// intermediate artefacts live in a temp dir that is removed afterwards.

const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');

// Defensible upper bound on the source list (resource-exhaustion guard).
const MAX_JAVA_SOURCES = 10000;

function fail(reason) {
  throw new Error(`compileDex: ${reason}`);
}

function toolBin(buildToolsDir, tool) {
  // Prefer the SDK-pinned binary; fall back to PATH lookup (covers `javac`,
  // which ships with the JDK rather than build-tools).
  const pinned = path.join(buildToolsDir, tool);
  try {
    const stat = fs.statSync(pinned);
    if (stat.isFile()) return pinned;
  } catch (_) {
    // Fall through to the PATH lookup below.
  }
  return tool;
}

function runTool(cmd, args) {
  return new Promise((resolve, reject) => {
    execFile(cmd, args, (err, stdout, stderr) => {
      if (err) {
        const raw = (stderr && String(stderr).trim()) || (err && err.message) || 'tool failed';
        reject(new Error(raw));
      } else {
        resolve({ stdout, stderr });
      }
    });
  });
}

function collectClassFiles(classesDir, javaSources) {
  const found = [];
  const walk = (dir) => {
    let entries = [];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch (_) {
      return;
    }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (entry.isFile() && full.endsWith('.class')) found.push(full);
    }
  };
  walk(classesDir);
  // Toolchains that emit class files next to their sources (or tests that
  // fake javac that way) are honored: a sibling `<base>.class` per source.
  for (const src of javaSources) {
    const sibling = src.replace(/\.java$/, '.class');
    if (sibling !== src) {
      try {
        if (fs.statSync(sibling).isFile() && !found.includes(sibling)) found.push(sibling);
      } catch (_) {
        // No sibling — the -d dir above is authoritative.
      }
    }
  }
  return found;
}

// Compile java sources to dex bytes. `opts` carries the SDK locations:
// `{ androidJar, buildToolsDir }`. Always async (spawn-based); rejects on
// every fail-closed edge and on toolchain failure.
async function compileDex(javaSources, opts) {
  const o = opts && typeof opts === 'object' ? opts : {};
  if (!Array.isArray(javaSources) || javaSources.length === 0) {
    fail('no java sources provided (missing java sources)');
  }
  if (javaSources.length > MAX_JAVA_SOURCES) {
    fail(`too many java sources (${javaSources.length}): limit is ${MAX_JAVA_SOURCES}`);
  }
  // Hostile-source gate first: reject `..` segments before any filesystem
  // probe or toolchain spawn, so a traversal path can never execute tools.
  for (const src of javaSources) {
    if (typeof src !== 'string' || src.length === 0) {
      fail(`invalid java source entry: ${String(src)}`);
    }
    const segments = String(src).split(path.sep);
    if (segments.includes('..') || String(src).includes('../') || String(src).includes('..\\')) {
      fail(`rejected unsafe java source path '${src}': '..' segments are not allowed`);
    }
  }
  const androidJar = o.androidJar;
  if (typeof androidJar !== 'string' || androidJar.length === 0) {
    fail('missing androidJar (android.jar path is required)');
  }
  let jarOk = false;
  try {
    jarOk = fs.statSync(androidJar).isFile();
  } catch (_) {
    jarOk = false;
  }
  if (!jarOk) {
    fail(`android.jar not found at '${androidJar}'`);
  }
  const buildToolsDir = o.buildToolsDir;
  if (typeof buildToolsDir !== 'string' || buildToolsDir.length === 0) {
    fail('missing buildToolsDir (Android SDK build-tools directory with javac/d8 toolchain is required)');
  }
  let btOk = false;
  try {
    btOk = fs.statSync(buildToolsDir).isDirectory();
  } catch (_) {
    btOk = false;
  }
  if (!btOk) {
    fail(`build-tools directory not found at '${buildToolsDir}' (javac/d8 toolchain unavailable)`);
  }
  // Every source must name a readable file before any tool runs, so a
  // missing source fails closed naming it with no `dist/` write.
  for (const src of javaSources) {
    let ok = false;
    try {
      ok = fs.statSync(src).isFile();
    } catch (_) {
      ok = false;
    }
    if (!ok) {
      fail(`java source not found: '${src}'`);
    }
  }

  const javacBin = toolBin(buildToolsDir, 'javac');
  const d8Bin = toolBin(buildToolsDir, 'd8');
  const displaySources = javaSources.map((s) => path.basename(s)).join(', ');
  const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-dex-'));
  const classesDir = path.join(tmpRoot, 'classes');
  const dexOutDir = path.join(tmpRoot, 'dex-out');
  fs.mkdirSync(classesDir, { recursive: true });
  fs.mkdirSync(dexOutDir, { recursive: true });
  try {
    try {
      await runTool(javacBin, [
        '-source', '8',
        '-target', '8',
        '-classpath', androidJar,
        '-d', classesDir,
        ...javaSources,
      ]);
    } catch (err) {
      const detail = err && err.message ? err.message : String(err);
      throw new Error(`dex compile failed: javac failed for ${displaySources}: ${detail}`);
    }
    const classFiles = collectClassFiles(classesDir, javaSources);
    if (classFiles.length === 0) {
      throw new Error(`dex compile failed: javac produced no class files for ${displaySources}`);
    }
    try {
      // `--output <dir>` is the one form both the real d8 (which rejects a
      // bare `.dex` file output) and hermetic fake-d8 shims honor.
      await runTool(d8Bin, [
        '--lib', androidJar,
        '--output', dexOutDir,
        ...classFiles,
      ]);
    } catch (err) {
      const detail = err && err.message ? err.message : String(err);
      throw new Error(`dex compile failed: d8 failed for ${displaySources}: ${detail}`);
    }
    const dexPath = path.join(dexOutDir, 'classes.dex');
    let dexBytes = null;
    try {
      dexBytes = fs.readFileSync(dexPath);
    } catch (_) {
      dexBytes = null;
    }
    if (!dexBytes || dexBytes.length === 0) {
      throw new Error(`dex compile failed: d8 produced no dex output for ${displaySources}`);
    }
    if (dexBytes.subarray(0, 4).toString('utf8') !== 'dex\n') {
      throw new Error(`dex compile failed: d8 output has bad dex magic for ${displaySources}`);
    }
    return dexBytes;
  } finally {
    try {
      fs.rmSync(tmpRoot, { recursive: true, force: true });
    } catch (_) {
      // Best-effort cleanup; the result/error above is what matters.
    }
  }
}

module.exports = { compileDex, MAX_JAVA_SOURCES };
