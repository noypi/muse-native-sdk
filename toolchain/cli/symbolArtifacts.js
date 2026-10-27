'use strict';
// T4 — Expose unstripped symbol artifacts for symbolication (NFR-BUILD-005 /
// AC-BUILD-005 symbolication half; NFR-BUILD-016 retention subset).
// Reads stub artifacts emitted alongside the T2 AAB in the project dir:
// unstripped `.so` files (`*-unstripped.so`), `strip-map.txt`, and
// `mapping.ref`. A stripped-only set is never reported ok.
const fs = require('node:fs');
const path = require('node:path');

const STRIP_MAP_FILENAME = 'strip-map.txt';
const MAPPING_FILENAME = 'mapping.ref';

function fail(reason) {
  throw new Error(`symbols-missing: ${reason}`);
}

function collectUnstrippedSo(projectDir) {
  const found = [];
  const stack = [projectDir];
  while (stack.length > 0) {
    const dir = stack.pop();
    let entries = [];
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true });
    } catch (err) {
      fail(`cannot read project: ${err && err.code ? err.code : 'EIO'}`);
    }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        stack.push(full);
      } else if (entry.isFile() && entry.name.endsWith('-unstripped.so')) {
        found.push(full);
      }
    }
  }
  return found.sort();
}

function collectSymbols(args) {
  const projectDir = args && args.projectDir;

  if (typeof projectDir !== 'string' || projectDir.length === 0) {
    fail('missing projectDir');
  }

  let dirStat = null;
  try {
    dirStat = fs.statSync(projectDir);
  } catch (err) {
    fail(`project not found: ${err && err.code ? err.code : 'ENOENT'}`);
  }
  if (!dirStat.isDirectory()) {
    fail('projectDir is not a directory');
  }

  const unstrippedSo = collectUnstrippedSo(projectDir);
  if (unstrippedSo.length === 0) {
    fail('no unstripped symbols retained');
  }
  for (const so of unstrippedSo) {
    let stat = null;
    try {
      stat = fs.statSync(so);
    } catch (err) {
      fail(`unstripped symbol missing: ${err && err.code ? err.code : 'ENOENT'}`);
    }
    if (!stat.isFile()) {
      fail('unstripped symbol missing');
    }
  }

  const stripMapPath = path.join(projectDir, STRIP_MAP_FILENAME);
  let mapStat = null;
  try {
    mapStat = fs.statSync(stripMapPath);
  } catch (err) {
    fail(`strip map missing: ${err && err.code ? err.code : 'ENOENT'}`);
  }
  if (!mapStat.isFile()) {
    fail('strip map missing');
  }

  const mappingPath = path.join(projectDir, MAPPING_FILENAME);
  let mappingStat = null;
  try {
    mappingStat = fs.statSync(mappingPath);
  } catch (err) {
    fail(`mapping ref missing: ${err && err.code ? err.code : 'ENOENT'}`);
  }
  if (!mappingStat.isFile()) {
    fail('mapping ref missing');
  }

  return { ok: true, unstrippedSo, stripMapPath, mappingRef: mappingPath };
}

module.exports = { collectSymbols };
