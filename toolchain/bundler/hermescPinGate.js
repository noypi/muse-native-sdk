'use strict';
// T4 — pinned hermesc CLI contract gate (NFR-BUILD-020 / AC-BUILD-076).
// Records the pinned hermesc version + exact invocation + bytecode target in
// sibling hermescPin.json; any drift fails before any artifact ships.
// Hermetic: sync stdlib fs/path/child_process only. No network, no secrets.

const fs = require('node:fs');
const path = require('node:path');
const childProcess = require('node:child_process');

class BundleError extends Error {
  constructor(code, detail) {
    super(String(detail));
    this.name = 'BundleError';
    this.code = code;
    this.detail = detail;
  }
}

function fail(code, detail) {
  throw new BundleError(code, detail);
}

function loadPin() {
  const pinPath = path.join(__dirname, 'hermescPin.json');
  let raw = null;
  try {
    raw = fs.readFileSync(pinPath, 'utf8');
  } catch (err) {
    void err;
    fail('EPINDRIFT', `hermesc pin record missing or unreadable: '${pinPath}'`);
  }
  let pin = null;
  try {
    pin = JSON.parse(raw);
  } catch (err) {
    void err;
    fail('EPINDRIFT', 'hermesc pin record is not valid JSON');
  }
  if (!pin || typeof pin.version !== 'string' || pin.version.length === 0) {
    fail('EPINDRIFT', 'hermesc pin record has no pinned version');
  }
  if (!pin.invocation || typeof pin.invocation.binary !== 'string' || pin.invocation.binary.length === 0) {
    fail('EPINDRIFT', 'hermesc pin record has no exact invocation');
  }
  if (typeof pin.bytecodeTarget !== 'string' || pin.bytecodeTarget.length === 0) {
    fail('EPINDRIFT', 'hermesc pin record has no bytecode target');
  }
  return pin;
}

function probeVersion(pin) {
  const binary = pin.invocation.binary;
  let output = null;
  try {
    if (typeof childProcess.execFileSync === 'function') {
      output = childProcess.execFileSync(binary, ['--version'], { encoding: 'utf8' });
    } else {
      output = childProcess.execSync(`${binary} --version`, { encoding: 'utf8' });
    }
  } catch (err) {
    void err;
    // Fall back to the other probe once before failing closed.
    try {
      output = childProcess.execSync(`${binary} --version`, { encoding: 'utf8' });
    } catch (err2) {
      void err2;
      fail('EPINDRIFT', `hermesc binary missing or not executable: '${binary}'`);
    }
  }
  const text = String(output == null ? '' : output);
  const match = text.match(/hermesc\s+version\s+([^\s]+)/i);
  if (!match) {
    fail('EPINDRIFT', 'hermesc version output unrecognized: contract drift');
  }
  return match[1].trim();
}

// checkHermescPin(): { ok: true, version } | throws BundleError('EPINDRIFT', detail)
function checkHermescPin() {
  const pin = loadPin();
  const actual = probeVersion(pin);
  if (actual !== pin.version) {
    fail('EPINDRIFT', `hermesc version drift: pinned '${pin.version}' but found '${actual}'`);
  }
  return { ok: true, version: pin.version };
}

module.exports = { checkHermescPin, BundleError };
