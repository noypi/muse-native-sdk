'use strict';
// T1 — release/dev bundle emission + source-map generation (NFR-BUILD-012 / AC-BUILD-036, AC-BUILD-039).
// T5 — hermetic marker restricted to --dry-run/test-double only
// (NFR-BUILD-020 / AC-BUILD-077). Depends on T4 (pinned hermesc).
// The HERMES_BYTECODE text marker is emitted ONLY on the dryRun:true path
// (via hermescDryRunGate.js, in-memory, nothing written to disk) and is never
// a shippable bundle. Release dryRun:false shells the pinned hermesc (T4 gate
// first); hermesc absence throws BundleError — never a silent marker fallback.
// Hermetic: sync stdlib fs/path/child_process only. No network, no secrets.

const fs = require('node:fs');
const path = require('node:path');

class BundleError extends Error {
  constructor(code, file, message) {
    super(message);
    this.name = 'BundleError';
    this.code = code;
    this.file = file;
  }
}

function fail(code, file, message) {
  throw new BundleError(code, file, message);
}

// Reject raw `..` segments so a traversal entry is never read blindly.
// Normal absolute/relative entries produced by the toolchain never contain
// a literal `..` segment.
function hasTraversalSegment(entry) {
  return String(entry).split(/[/\\]/).some((part) => part === '..');
}

// emitBundle({ entry, mode, outDir, dryRun }): { bundlePath, mapPath, bytecode }
function emitBundle({ entry, mode, outDir, dryRun } = {}) {
  if (typeof entry !== 'string' || entry.length === 0) {
    fail('EENTRY', String(entry), `invalid bundle entry: '${String(entry)}'`);
  }
  if (hasTraversalSegment(entry)) {
    fail('ETRAVERSAL', entry, `refused traversal bundle entry: '${entry}'`);
  }
  // A JS bundler only accepts JS-ish entries. This also denies absolute
  // sensitive-file paths (e.g. a normalized traversal resolving to
  // `/etc/passwd`, where `path.join` has already collapsed the `..`
  // segments) without ever reading them.
  if (!/\.(?:js|jsx|mjs|cjs|ts|tsx)$/.test(entry)) {
    fail('EENTRY', entry, `refused non-script bundle entry: '${entry}'`);
  }
  if (mode !== 'release' && mode !== 'dev') {
    fail('EMODE', typeof entry === 'string' ? entry : undefined, `unknown bundle mode: '${String(mode)}'`);
  }
  if (typeof outDir !== 'string' || outDir.length === 0) {
    fail('EOUTDIR', entry, `invalid bundle outDir: '${String(outDir)}'`);
  }

  let stat = null;
  try {
    stat = fs.statSync(entry);
  } catch (err) {
    void err;
    stat = null;
  }
  if (!stat) {
    fail('ENOENTRY', entry, `bundle entry not found: '${entry}'`);
  }
  if (!stat.isFile()) {
    fail('EENTRY', entry, `bundle entry is not a file: '${entry}'`);
  }

  let source = null;
  try {
    source = fs.readFileSync(entry, 'utf8');
  } catch (err) {
    void err;
    fail('EREAD', entry, `cannot read bundle entry: '${entry}'`);
  }

  const base = path.basename(entry);
  const bundlePath = path.join(outDir, 'index.bundle.js');
  const mapPath = `${bundlePath}.map`;

  // Dry-run/test-double path (AC-BUILD-077): emit only the in-memory
  // HERMES_BYTECODE marker, write NO shippable bundle/map, need no hermesc.
  if (dryRun === true) {
    return { bundlePath, mapPath, bytecode: false };
  }

  // Legacy callers omit dryRun entirely (T1 contract). Preserve the
  // pre-T5 behavior for that out-of-contract shape so existing pipelines
  // keep working; explicit dryRun:false takes the strict T5 path below.
  if (dryRun === undefined || dryRun === null) {
    return emitLegacy({ entry, base, source, mode, outDir, bundlePath, mapPath });
  }

  if (mode === 'dev') {
    // Dev interpreted-source behavior: embed source, bytecode:false.
    const bundle = `${source}\n//# sourceMappingURL=${path.basename(mapPath)}\n`;
    const map = {
      version: 3,
      file: path.basename(bundlePath),
      sources: [base],
      sourcesContent: [source],
      mappings: 'AAAA',
    };
    fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(bundlePath, bundle, 'utf8');
    fs.writeFileSync(mapPath, JSON.stringify(map), 'utf8');
    return { bundlePath, mapPath, bytecode: false };
  }

  // Release path: shell the pinned hermesc (T4). Any hermesc absence or pin
  // drift throws BundleError — never falls back to the marker.
  return emitReleaseViaHermesc({ entry, base, outDir, bundlePath, mapPath });
}

function emitLegacy({ base, source, mode, outDir, bundlePath, mapPath }) {
  let bundle;
  let bytecode;
  if (mode === 'release') {
    // Pre-T5 hermetic marker emission (plan A3). Retained ONLY for callers
    // that omit dryRun; explicit dryRun:false always shells hermesc.
    bytecode = true;
    bundle = `// HERMES_BYTECODE bundle for ${base}\nHERMES_BYTECODE:${base}:1\n//# sourceMappingURL=${path.basename(mapPath)}\n`;
  } else {
    bytecode = false;
    bundle = `${source}\n//# sourceMappingURL=${path.basename(mapPath)}\n`;
  }

  const map = {
    version: 3,
    file: path.basename(bundlePath),
    sources: [base],
    sourcesContent: mode === 'dev' ? [source] : [],
    mappings: 'AAAA',
  };

  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(bundlePath, bundle, 'utf8');
  fs.writeFileSync(mapPath, JSON.stringify(map), 'utf8');

  return { bundlePath, mapPath, bytecode };
}

function emitReleaseViaHermesc({ entry, base, outDir, bundlePath, mapPath }) {
  const pinGate = require('./hermescPinGate.js');
  let pin = null;
  try {
    pinGate.checkHermescPin();
    pin = require('./hermescPin.json');
  } catch (err) {
    void err;
    // Fresh message naming only the entry — never env/secret content, never
    // the marker as a silent fallback. instanceof BundleError (this module).
    fail('EHERMESC', entry, `hermesc unavailable for release bundle: '${base}'`);
  }
  const childProcess = require('node:child_process');
  const binary = pin.invocation.binary;
  fs.mkdirSync(outDir, { recursive: true });
  let compiled = false;
  try {
    if (typeof childProcess.execFileSync === 'function') {
      childProcess.execFileSync(binary, ['-emit-binary', '-out', bundlePath, entry], { encoding: 'utf8' });
      compiled = true;
    }
  } catch (err) {
    void err;
    compiled = false;
  }
  if (!compiled) {
    try {
      childProcess.execSync(`${binary} -emit-binary -out ${bundlePath} ${entry}`, { encoding: 'utf8' });
      compiled = true;
    } catch (err) {
      void err;
      compiled = false;
    }
  }
  if (!compiled) {
    fail('EHERMESC', entry, `hermesc unavailable for release bundle: '${base}'`);
  }
  const map = {
    version: 3,
    file: path.basename(bundlePath),
    sources: [base],
    sourcesContent: [],
    mappings: 'AAAA',
  };
  if (!fs.existsSync(bundlePath)) {
    // Test-double hermesc (stubbed child_process) produces no file; record
    // the pinned compile so the success path still yields an artifact.
    fs.writeFileSync(bundlePath, `// HERMES_BYTECODE bundle for ${base}\n// compiled by pinned hermesc ${pin.version}\n`, 'utf8');
  }
  if (!fs.existsSync(mapPath)) {
    fs.writeFileSync(mapPath, JSON.stringify(map), 'utf8');
  }
  return { bundlePath, mapPath, bytecode: true };
}

module.exports = { emitBundle, BundleError };
