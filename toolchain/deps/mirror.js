'use strict';
// T2 — content-keyed local mirror with zero-network offline re-run.
// (FR-DEPS-009 AC-DEPS-023).
//
// fetchToMirror(artifact, abi, opts) -> { path, hit }.
// - Mirror lives under toolchain/deps/mirror/, keyed by sha256 of content.
// - An index (mirror/index.json) maps artifact+ABI -> sha256 so a warm
//   mirror (online or offline) is served with zero network calls.
// - Cold offline throws SetupError EOFFLINE naming artifact+ABI before
//   any network attempt. Unknown ABI fails closed (EUNPINNED), also
//   before any network attempt.
// - All network traffic goes through globalThis.fetch (stub-observable).
// - Artifact strings are never used as filesystem paths (only as index
//   keys and error fields), so hostile names cannot escape the mirror.

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const SUPPORTED_ABIS = ['arm64-v8a', 'armeabi-v7a', 'x86', 'x86_64'];

const SHA256_RE = /^[0-9a-fA-F]{64}$/;

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

function mirrorRoot() {
  return path.join(__dirname, 'mirror');
}

function indexFile() {
  return path.join(mirrorRoot(), 'index.json');
}

function loadIndex() {
  let data;
  try {
    data = JSON.parse(fs.readFileSync(indexFile(), 'utf8'));
  } catch {
    return {};
  }
  if (data === null || typeof data !== 'object' || Array.isArray(data)) return {};
  return data;
}

function saveIndex(index) {
  fs.mkdirSync(mirrorRoot(), { recursive: true });
  fs.writeFileSync(indexFile(), `${JSON.stringify(index, null, 2)}\n`, 'utf8');
}

function indexKey(artifact, abi) {
  return `${abi}\0${artifact}`;
}

function pinnedUrl(artifact, abi) {
  try {
    const pins = JSON.parse(fs.readFileSync(path.join(__dirname, 'pins.json'), 'utf8'));
    const entries = pins[abi];
    if (Array.isArray(entries)) {
      for (const entry of entries) {
        if (
          entry !== null &&
          typeof entry === 'object' &&
          (entry.artifact === artifact || entry.name === artifact) &&
          typeof entry.url === 'string' &&
          entry.url.length > 0
        ) {
          return entry.url;
        }
      }
    }
  } catch {
    // No usable pins file — fall through to the synthetic URL.
  }
  return `https://example.com/native-deps/${encodeURIComponent(artifact)}-${encodeURIComponent(abi)}.tar.gz`;
}

async function fetchToMirror(artifact, abi, opts) {
  const offline = opts !== undefined && opts !== null && opts.offline === true;

  if (typeof abi !== 'string' || !SUPPORTED_ABIS.includes(abi)) {
    throw new SetupError('EUNPINNED', artifact, abi);
  }
  if (typeof artifact !== 'string' || artifact.length === 0) {
    if (offline) throw new SetupError('EOFFLINE', artifact, abi);
    throw new SetupError('EUNPINNED', artifact, abi);
  }

  const key = indexKey(artifact, abi);
  const index = loadIndex();
  const knownSha = typeof index[key] === 'string' && SHA256_RE.test(index[key]) ? index[key] : undefined;
  if (knownSha !== undefined) {
    const knownPath = path.join(mirrorRoot(), `${knownSha}.bin`);
    try {
      const stat = fs.statSync(knownPath);
      if (stat.isFile()) return { path: knownPath, hit: true };
    } catch {
      // Indexed but file missing — fall through to (online) refetch or
      // (offline) fail closed below.
    }
  }

  if (offline) {
    throw new SetupError('EOFFLINE', artifact, abi);
  }

  const url = pinnedUrl(artifact, abi);
  const res = await globalThis.fetch(url);
  if (res !== null && typeof res === 'object' && typeof res.ok === 'boolean' && !res.ok) {
    throw new SetupError('EOFFLINE', artifact, abi);
  }
  let bytes;
  if (res !== null && typeof res === 'object' && typeof res.arrayBuffer === 'function') {
    bytes = Buffer.from(await res.arrayBuffer());
  } else {
    bytes = Buffer.from(String(res));
  }
  const sha256 = crypto.createHash('sha256').update(bytes).digest('hex');
  const filePath = path.join(mirrorRoot(), `${sha256}.bin`);
  let hit = false;
  try {
    const stat = fs.statSync(filePath);
    if (stat.isFile()) hit = true;
  } catch {
    hit = false;
  }
  if (!hit) {
    fs.mkdirSync(mirrorRoot(), { recursive: true });
    fs.writeFileSync(filePath, bytes);
  }
  const fresh = loadIndex();
  fresh[key] = sha256;
  saveIndex(fresh);
  return { path: filePath, hit };
}

module.exports = { fetchToMirror, SetupError, SUPPORTED_ABIS };
