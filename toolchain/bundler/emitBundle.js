'use strict';
// T1 — release/dev bundle emission + source-map generation (NFR-BUILD-012 / AC-BUILD-036, AC-BUILD-039).
// Hermetic: sync stdlib fs/path only. Release bytecode is a hermetic marker
// emission per plan A3 (text marker "HERMES_BYTECODE"); real hermesc wiring
// is an explicit follow-up.

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

// emitBundle({ entry, mode, outDir }): { bundlePath, mapPath, bytecode }
function emitBundle({ entry, mode, outDir } = {}) {
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

  let bundle;
  let bytecode;
  if (mode === 'release') {
    // Hermetic marker emission (plan A3): identifies a release bundle without
    // invoking the hermesc binary. Deliberately omits interpreted source.
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

module.exports = { emitBundle, BundleError };
