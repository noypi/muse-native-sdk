'use strict';
// T3 — Play delivery fakes (NFR-BUILD-005 / AC-BUILD-005 mount half; consumes
// NFR-BUILD-010 delivery-shape names only).
// Hermetic fake device: packs live in tmpdir, progress is deterministic and
// driven by polling monitorPack (each call is one synchronous step).
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

let nextHandle = 0;
const packsByHandle = new Map();
const requestsByName = new Map();

function fail(reason, retries) {
  const err = new Error(`delivery-failed: ${reason}`);
  err.retries = retries;
  throw err;
}

function isRetryFixtureName(name) {
  return name.indexOf('retry') !== -1;
}

function stateFor(record) {
  if (record.mode === 'will-fail') {
    if (record.polls === 0) return { state: 'pending', pct: 0 };
    if (record.polls === 1) return { state: 'downloading', pct: 50 };
    return { state: 'failed', pct: 50 };
  }
  if (record.polls === 0) return { state: 'pending', pct: 0 };
  if (record.polls === 1) return { state: 'downloading', pct: 50 };
  return { state: 'ready', pct: 100 };
}

function requestPack(name) {
  if (typeof name !== 'string' || name.length === 0) {
    fail('invalid pack name', 0);
  }
  const seen = requestsByName.get(name) || 0;
  requestsByName.set(name, seen + 1);
  // Failing-first-attempt fixture: the first request for a pack name
  // containing `retry` ends `failed`; re-requesting the same name resumes to
  // a pack that reaches `ready`.
  const mode = isRetryFixtureName(name) && seen === 0 ? 'will-fail' : 'ok';
  nextHandle += 1;
  const handle = `handle-${nextHandle}`;
  const mountPoint = fs.mkdtempSync(path.join(os.tmpdir(), 'play-pack-'));
  try {
    fs.writeFileSync(path.join(mountPoint, 'pack.marker.txt'), name, 'utf8');
  } catch {
    // Best-effort marker; the directory itself is the mount stand-in.
  }
  packsByHandle.set(handle, {
    packName: name,
    mode,
    polls: 0,
    mountPoint,
    retries: mode === 'will-fail' ? 1 : 0,
  });
  return { handle };
}

function monitorPack(handle) {
  if (typeof handle !== 'string' || handle.length === 0) {
    fail('unknown handle', 0);
  }
  const record = packsByHandle.get(handle);
  if (!record) {
    fail('unknown handle', 0);
  }
  const current = stateFor(record);
  record.polls += 1;
  return current;
}

function mountPack(handle) {
  if (typeof handle !== 'string' || handle.length === 0) {
    fail('unknown handle', 0);
  }
  const record = packsByHandle.get(handle);
  if (!record) {
    fail('unknown handle', 0);
  }
  const current = stateFor({ mode: record.mode, polls: record.polls });
  if (current.state === 'failed') {
    fail(`pack download failed for ${record.packName}`, record.retries >= 1 ? record.retries : 1);
  }
  if (current.state !== 'ready') {
    fail(`pack not ready: ${current.state}`, 0);
  }
  return { mountPoint: record.mountPoint };
}

module.exports = { requestPack, monitorPack, mountPack };
