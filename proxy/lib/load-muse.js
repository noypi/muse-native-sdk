'use strict';
// T7 — per-project muse artefact loader (AC-MUSE-046, A2).
// Reads ONLY the explicit path <appDir>/dist/muse.json (never parent-dir
// search, never the repo-root file per RULE-WORKFLOW-007; appDir comes from
// ctx.appDir, never process.cwd() sniffing). Sync-read/fail-closed pattern
// mirrors proxy/lib/load-config.js:48-62 (fs.readFileSync, no network,
// no key/env access). Values returned are NOT trusted (allow-list
// re-validation stays in dispatch per T3-hardening): load-and-attach only.

const fs = require('node:fs');
const path = require('node:path');
const { ProxyError } = require('./errors');

function isRecord(value) {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function fail() {
  throw new ProxyError('schema', 'muse.json', false);
}

// loadProjectMuse(appDir): absent file returns route defaults
// ({ models: {}, presets: {} }); present-but-corrupt (unreadable /
// unparseable JSON / non-record { models, presets } shape) throws
// fail-closed ProxyError pre-upstream, never a partial object.
function loadProjectMuse(appDir) {
  const artefactPath = path.join(String(appDir), 'dist', 'muse.json');
  let raw;
  try {
    raw = fs.readFileSync(artefactPath, 'utf8');
  } catch (err) {
    // Absent file is the graceful default (route defaults); any other
    // read failure (unreadable path, muse.json is a directory) fails closed.
    if (err && err.code === 'ENOENT') return { models: {}, presets: {} };
    fail();
  }

  let doc;
  try {
    doc = JSON.parse(raw);
  } catch {
    fail();
  }

  if (!isRecord(doc) || !isRecord(doc.models) || !isRecord(doc.presets)) {
    fail();
  }

  return doc;
}

module.exports = { loadProjectMuse };
