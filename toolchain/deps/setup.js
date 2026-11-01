'use strict';
// T1 — per-ABI pinned native-dep manifest resolution (fail closed).
// Reads toolchain/deps/pins.json; never fetches or compiles.

const fs = require('node:fs');
const path = require('node:path');

const SUPPORTED_ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'];

const SHA256_RE = /^[0-9a-fA-F]{64}$/;
const EXACT_VERSION_RE = /^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$/;
const HTTPS_RE = /^https:\/\//;

class SetupError extends Error {
  constructor(code, artifact, abi, check) {
    super(`${code}: ${String(artifact)} for ${String(abi)}`);
    this.name = 'SetupError';
    this.code = code;
    this.artifact = artifact;
    this.abi = abi;
    if (check !== undefined) this.check = check;
  }
}

function loadPins() {
  const file = path.join(__dirname, 'pins.json');
  let raw;
  try {
    raw = fs.readFileSync(file, 'utf8');
  } catch {
    throw new SetupError('EUNPINNED', 'unknown', 'unknown');
  }
  let data;
  try {
    data = JSON.parse(raw);
  } catch {
    throw new SetupError('EUNPINNED', 'unknown', 'unknown');
  }
  if (data === null || typeof data !== 'object' || Array.isArray(data)) {
    throw new SetupError('EUNPINNED', 'unknown', 'unknown');
  }
  return data;
}

function resolvePins(abi) {
  const pins = loadPins();
  if (typeof abi !== 'string' || abi.length === 0 || !SUPPORTED_ABIS.includes(abi)) {
    throw new SetupError('EUNPINNED', 'unknown', abi);
  }
  const entries = pins[abi];
  if (!Array.isArray(entries) || entries.length === 0) {
    throw new SetupError('EUNPINNED', 'unknown', abi);
  }
  const out = [];
  for (const entry of entries) {
    const artifact =
      entry !== null && typeof entry === 'object' && !Array.isArray(entry) &&
      typeof entry.artifact === 'string' && entry.artifact.length > 0
        ? entry.artifact
        : typeof entry.name === 'string' && entry.name.length > 0
          ? entry.name
          : 'unknown';
    if (entry === null || typeof entry !== 'object' || Array.isArray(entry)) {
      throw new SetupError('EUNPINNED', artifact, abi);
    }
    if (!EXACT_VERSION_RE.test(entry.version)) {
      throw new SetupError('EUNPINNED', artifact, abi);
    }
    if (typeof entry.sha256 !== 'string' || !SHA256_RE.test(entry.sha256)) {
      throw new SetupError('EHASH', artifact, abi);
    }
    if (typeof entry.url !== 'string' || entry.url.length === 0 || !HTTPS_RE.test(entry.url)) {
      throw new SetupError('EUNPINNED', artifact, abi);
    }
    out.push({ artifact, version: entry.version, sha256: entry.sha256, url: entry.url });
  }
  return out;
}

module.exports = { resolvePins, SetupError, SUPPORTED_ABIS };
