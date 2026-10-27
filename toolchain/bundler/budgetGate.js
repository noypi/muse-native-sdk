'use strict';
// T2 — initial-bundle budget gate (NFR-BUILD-012 / AC-BUILD-037, AC-BUILD-040).
// Hermetic: sync stdlib fs only. Budget value is an INPUT (perf-owned).

const fs = require('node:fs');

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

// checkBudget({ bundlePath, budgetBytes, manifest }): { ok, totalBytes, offender? }
function checkBudget({ bundlePath, budgetBytes, manifest } = {}) {
  if (typeof bundlePath !== 'string' || bundlePath.length === 0) {
    fail('BAD_INPUT', String(bundlePath), `invalid bundle path: '${String(bundlePath)}'`);
  }
  let stat = null;
  try {
    stat = fs.statSync(bundlePath);
  } catch (err) {
    void err;
    stat = null;
  }
  if (!stat) {
    fail('BAD_INPUT', bundlePath, `bundle file not found: '${bundlePath}'`);
  }
  if (!stat.isFile()) {
    fail('BAD_INPUT', bundlePath, `bundle path is not a file: '${bundlePath}'`);
  }
  if (typeof budgetBytes !== 'number' || !Number.isFinite(budgetBytes) || budgetBytes < 0) {
    fail('BAD_INPUT', bundlePath, `invalid budget value: '${String(budgetBytes)}'`);
  }
  if (!Array.isArray(manifest)) {
    fail('BAD_INPUT', bundlePath, 'invalid manifest: expected an array');
  }
  let totalBytes = 0;
  let offender = '';
  let largest = -1;
  for (const entry of manifest) {
    if (!entry || typeof entry !== 'object' || typeof entry.module !== 'string' || entry.module.length === 0) {
      fail('BAD_INPUT', bundlePath, 'invalid manifest entry: expected { module: string, bytes: number }');
    }
    if (typeof entry.bytes !== 'number' || !Number.isFinite(entry.bytes) || entry.bytes < 0) {
      fail('BAD_INPUT', bundlePath, `invalid manifest entry bytes for module: '${entry.module}'`);
    }
    totalBytes += entry.bytes;
    if (entry.bytes > largest) {
      largest = entry.bytes;
      offender = entry.module;
    }
  }
  if (totalBytes <= budgetBytes) {
    return { ok: true, totalBytes };
  }
  return { ok: false, totalBytes, offender };
}

module.exports = { checkBudget, BundleError };
