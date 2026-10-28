'use strict';
// T3 — FR-DX-008 / AC-DX-034 / TC-DX-034.
// Symbolication seam: consumed by tools/cli/lib/logcat-stream.js.
// Production default: no device attached here; tests replace this module
// in-process via require.cache pre-seeding with an in-memory fake exposing
// symbolicate(frame) and optional readLines(filter).

function symbolicate(frame) {
  return { frame: String(frame), mappedSource: null };
}

function readLines() {
  return [];
}

module.exports = { symbolicate, readLines };
