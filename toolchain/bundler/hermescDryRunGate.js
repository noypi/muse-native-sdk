'use strict';
// T5 — hermetic marker restricted to --dry-run/test-double only
// (NFR-BUILD-020 / AC-BUILD-077). Depends on T4 (pinned hermesc).
// Hermetic: sync stdlib only. No network, no secrets, no live services.
// The hermesc collaborator (node:child_process) is stubbed in-process by tests.

const DRY_RUN_MARKER = 'HERMES_BYTECODE';

// isDryRun(opts): true only when the caller explicitly passes dryRun:true.
// The marker/test-double path is never a shippable bundle.
function isDryRun(opts) {
  return Boolean(opts) && opts.dryRun === true;
}

// dryRunResult({ entry, outDir }): computes the would-be artifact paths for a
// dry-run without writing anything. The HERMES_BYTECODE text survives only as
// this in-memory marker path — never as bytes on disk.
function dryRunResult({ entry, outDir } = {}) {
  const path = require('node:path');
  const bundlePath = path.join(String(outDir), 'index.bundle.js');
  const mapPath = `${bundlePath}.map`;
  void entry;
  return { bundlePath, mapPath, bytecode: false, marker: DRY_RUN_MARKER };
}

module.exports = { DRY_RUN_MARKER, isDryRun, dryRunResult };
