'use strict';
// T3 — lazy fail-closed third-party resolver (NFR-BUILD-021 / AC-BUILD-078/079).
// Dep-needing build targets resolve uncommitted third-party tarballs from
// $MUSE_NATIVE_THIRD_PARTY_DIR, verify sha256 pins, and extract on demand into
// the build tree. Dep-free targets keep building with the env unset.
// Hermetic: sync stdlib fs/path/crypto/child_process only. No network.

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const childProcess = require('node:child_process');

const VAR_NAME = 'MUSE_NATIVE_THIRD_PARTY_DIR';
const EXAMPLE_EXPORT = 'export MUSE_NATIVE_THIRD_PARTY_DIR=<shared-workspace>/third-party';

function withVarHelp(detail) {
  return `${detail}. Set ${VAR_NAME} to the shared third-party directory. Example: ${EXAMPLE_EXPORT}`;
}

class ThirdPartyError extends Error {
  constructor(code, message) {
    super(String(message));
    this.name = 'ThirdPartyError';
    this.code = code;
  }
}

function fail(code, detail) {
  throw new ThirdPartyError(code, withVarHelp(detail));
}

function defaultPinsPath() {
  return path.resolve(__dirname, '..', '..', 'tools', 'deps', 'third_party_pins.json');
}

function defaultBuildTree() {
  return path.resolve(process.cwd(), 'build');
}

function isHex64(value) {
  return typeof value === 'string' && /^[0-9a-fA-F]{64}$/.test(value);
}

// requiresThirdParty(depName): false only for the no-dep path (missing/empty
// dep name). Any non-empty dep name requires third-party resolution, so an
// unknown dep still throws ETHIRDPARTYPIN rather than silently passing.
function requiresThirdParty(depName) {
  if (typeof depName !== 'string') {
    return false;
  }
  return depName.length > 0;
}

function loadPin(pinsPath, depName) {
  let raw = null;
  try {
    raw = fs.readFileSync(pinsPath, 'utf8');
  } catch (err) {
    void err;
    fail('ETHIRDPARTYPIN', `third-party pin manifest missing or unreadable: '${pinsPath}' (dep '${depName}')`);
  }
  let pins = null;
  try {
    pins = JSON.parse(raw);
  } catch (err) {
    void err;
    fail('ETHIRDPARTYPIN', `third-party pin manifest is not valid JSON: '${pinsPath}' (dep '${depName}')`);
  }
  if (!pins || typeof pins !== 'object' || Array.isArray(pins)) {
    fail('ETHIRDPARTYPIN', `third-party pin manifest is not an object: '${pinsPath}' (dep '${depName}')`);
  }
  const entry = pins[depName];
  if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
    fail('ETHIRDPARTYPIN', `no third-party pin for dep '${depName}' in '${pinsPath}'`);
  }
  if (typeof entry.file !== 'string' || entry.file.length === 0) {
    fail('ETHIRDPARTYPIN', `third-party pin for dep '${depName}' has no tarball file in '${pinsPath}'`);
  }
  if (!isHex64(entry.sha256)) {
    fail('ETHIRDPARTYPIN', `third-party pin for dep '${depName}' has no 64-char hex sha256 in '${pinsPath}'`);
  }
  if (path.isAbsolute(entry.file) || entry.file.split(path.sep).includes('..')) {
    fail('ETHIRDPARTYPIN', `third-party pin for dep '${depName}' must name a bare tarball filename in '${pinsPath}'`);
  }
  return { file: path.basename(entry.file), sha256: entry.sha256.toLowerCase() };
}

function sha256File(filePath) {
  const data = fs.readFileSync(filePath);
  return crypto.createHash('sha256').update(data).digest('hex').toLowerCase();
}

function markerPath(outDir) {
  return path.join(outDir, '.third-party-pin.json');
}

function readMarker(outDir) {
  try {
    const raw = fs.readFileSync(markerPath(outDir), 'utf8');
    return JSON.parse(raw);
  } catch (err) {
    void err;
    return null;
  }
}

function writeMarker(outDir, record) {
  fs.writeFileSync(markerPath(outDir), JSON.stringify(record), 'utf8');
}

function shQuote(value) {
  return `'${String(value).replace(/'/g, `'\\''`)}'`;
}

function listTarMembers(tarballPath) {
  try {
    if (typeof childProcess.execFileSync === 'function') {
      const out = childProcess.execFileSync('tar', ['-tzf', tarballPath], { encoding: 'utf8' });
      return String(out).split('\n');
    }
    const out = childProcess.execSync(`tar -tzf ${shQuote(tarballPath)}`, { encoding: 'utf8' });
    return String(out).split('\n');
  } catch (err) {
    void err;
    fail('ETHIRDPARTYENTRY', `failed to list third-party tarball members '${tarballPath}'`);
  }
  return [];
}

function assertSafeTarMembers(members, tarballPath) {
  for (const raw of members) {
    const member = String(raw).trim();
    if (member.length === 0) {
      continue;
    }
    if (member.startsWith('/') || member.split('/').includes('..')) {
      fail('ETHIRDPARTYENTRY', `third-party tarball '${tarballPath}' contains unsafe member path '${member}'`);
    }
  }
}

function extractTarball(tarballPath, outDir) {
  assertSafeTarMembers(listTarMembers(tarballPath), tarballPath);
  fs.mkdirSync(outDir, { recursive: true });
  try {
    if (typeof childProcess.execFileSync === 'function') {
      childProcess.execFileSync('tar', ['-xzf', tarballPath, '-C', outDir]);
      return;
    }
    childProcess.execSync(`tar -xzf ${shQuote(tarballPath)} -C ${shQuote(outDir)}`);
  } catch (err) {
    void err;
    // Fall back to the string form once before failing closed.
    try {
      childProcess.execSync(`tar -xzf ${shQuote(tarballPath)} -C ${shQuote(outDir)}`);
    } catch (err2) {
      void err2;
      fail('ETHIRDPARTYHASH', `failed to extract third-party tarball '${tarballPath}'`);
    }
  }
}

// resolveThirdParty(depName, opts?): { dir } | throws ThirdPartyError
function resolveThirdParty(depName, opts) {
  const options = opts && typeof opts === 'object' ? opts : {};
  const pinsPath = typeof options.pinsPath === 'string' && options.pinsPath.length > 0
    ? options.pinsPath
    : defaultPinsPath();
  const buildTree = typeof options.buildTree === 'string' && options.buildTree.length > 0
    ? options.buildTree
    : defaultBuildTree();

  // No-dep path: never touches the env (AC-BUILD-079 lazy property).
  if (!requiresThirdParty(depName)) {
    return { dir: buildTree };
  }

  // 1. Env (unset/empty -> ETHIRDPARTYENV).
  const dir = process.env[VAR_NAME];
  if (typeof dir !== 'string' || dir.length === 0) {
    fail('ETHIRDPARTYENV', `${VAR_NAME} is not set (dep '${depName}')`);
  }

  // 2. Folder (missing/not-a-dir -> ETHIRDPARTYDIR).
  let stat = null;
  try {
    stat = fs.statSync(dir);
  } catch (err) {
    void err;
    fail('ETHIRDPARTYDIR', `third-party directory missing: '${dir}' (dep '${depName}')`);
  }
  if (!stat.isDirectory()) {
    fail('ETHIRDPARTYDIR', `third-party directory is not a directory: '${dir}' (dep '${depName}')`);
  }

  // 3. Pin for depName (missing/invalid -> ETHIRDPARTYPIN).
  const pin = loadPin(pinsPath, depName);

  // 4. sha256 the $DIR/<file> tarball (missing/mismatch -> ETHIRDPARTYHASH).
  // Always re-verified, including the marker-cache-hit path below, so a
  // tarball tampered with after first extract cannot hide behind the marker.
  const tarballPath = path.join(dir, pin.file);
  function verifyTarballHash() {
    let actual = null;
    try {
      actual = sha256File(tarballPath);
    } catch (err) {
      void err;
      fail('ETHIRDPARTYHASH', `third-party tarball missing or unreadable: '${tarballPath}' (dep '${depName}')`);
    }
    if (actual !== pin.sha256) {
      fail('ETHIRDPARTYHASH', `third-party tarball hash mismatch for dep '${depName}': expected '${pin.sha256}' but found '${actual}'`);
    }
  }

  // 5. Extract that single tarball on demand into the build tree (per-dep
  // only, never in-source). Skip when the output already matches the pin.
  const outDir = path.join(buildTree, 'third-party', depName);
  const marker = readMarker(outDir);
  if (marker && marker.sha256 === pin.sha256 && marker.file === pin.file) {
    verifyTarballHash();
    return { dir: outDir };
  }
  verifyTarballHash();
  extractTarball(tarballPath, outDir);
  writeMarker(outDir, { dep: depName, file: pin.file, sha256: pin.sha256 });
  return { dir: outDir };
}

module.exports = { resolveThirdParty, requiresThirdParty, ThirdPartyError };
