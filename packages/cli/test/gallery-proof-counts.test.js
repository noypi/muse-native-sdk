'use strict';
// T1 — `parseDeviceCounts` per-line pair-parsing regression (BUG-M3-FIDELITY-012).
// Spec refs: AC-M3-020 (FR-M3-009, SPEC-M3-NATIVE), TC-M3-021.
// Interface (plan.md verbatim):
//   `parseDeviceCounts(text: string): { rows: number, pages: number } | null` — pure helper
//   under test (exported from `packages/cli/lib/gallery-proof.js`); throws nothing, returns
//   `null` when no complete per-line pair exists (caller maps `null` to `counts-mismatch`);
//   error shape: assertion failures only.
// Hermetic: node:test + node:assert/strict only. No adb, no device, no network, no config.
// Reference only (not imported for behavior): the `readCounts` double pattern in
// `packages/cli/test/gallery-parity.test.js`.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `text` is the raw `dumpsys + logcat` buffer; parsing is per-line, so a `/.../`
//   regex without the `s` flag (or an explicit line split) is the observable boundary.
// - A2: non-string/empty input yields `null` (throws nothing) — the caller maps it to
//   `counts-mismatch`, never a pass.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const galleryProof = require('../lib/gallery-proof.js');

function parseDeviceCounts(text) {
  assert.equal(typeof galleryProof.parseDeviceCounts, 'function',
    'lib/gallery-proof.js must export parseDeviceCounts(text)');
  return galleryProof.parseDeviceCounts(text);
}

describe('T1 — stale load-only line + fresh pair yields the fresh pair (AC-M3-020)', () => {
  it('stale gallery-rows=110 load-only line + fresh rows=121 pages=12 pair yields {rows:121, pages:12}', () => {
    const text = [
      '01-01 00:00:01.000  1234  1234 I GalleryPage: gallery-rows=110',
      '01-01 00:00:02.000  1234  1234 I GalleryPage: gallery-rows=121 gallery-pages=12',
    ].join('\n');
    assert.deepEqual(parseDeviceCounts(text), { rows: 121, pages: 12 });
  });

  it('last complete per-line pair wins when two complete pairs exist', () => {
    const text = [
      'GalleryPage: gallery-rows=110 gallery-pages=11',
      'GalleryPage: gallery-rows=121 gallery-pages=12',
    ].join('\n');
    assert.deepEqual(parseDeviceCounts(text), { rows: 121, pages: 12 });
  });
});

describe('T1 — fail-closed when no complete per-line pair exists (AC-M3-020)', () => {
  it('stale-only buffer (lone gallery-rows lines) yields null, never a spliced pair', () => {
    const text = [
      'GalleryPage: gallery-rows=110',
      'GalleryPage: gallery-rows=110',
      'GalleryPage: gallery-pages=12',
    ].join('\n');
    assert.equal(parseDeviceCounts(text), null);
  });

  it('empty buffer yields null and throws nothing', () => {
    assert.equal(parseDeviceCounts(''), null);
  });

  it('unrelated log text yields null and throws nothing', () => {
    assert.equal(parseDeviceCounts('some unrelated logcat line without markers\nanother line'), null);
  });
});
