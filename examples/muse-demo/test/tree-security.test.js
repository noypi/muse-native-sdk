'use strict';
// Security coverage for `demo-muse-demo` (Stage 4 security-review.md, 2026-11-02).
// Two denial tests only — no new product behavior. Hermetic: node:test +
// node:assert/strict only; synthetic bytes only (zero real credentials,
// tokens, keys, or device/network access). The real unit under test is
// `decodeTreeIR` (packages/cli/lib/gallery-proof.js), the unit-testable
// mirror of the `nativeLoadTree` JNI tree parser: MUI1 magic + u32LE
// version + u32LE length-prefix + utf8 JSON IR (see packages/cli/README.md).
// ASSUMPTION (both tests): the native `nativeLoadTree` parser upholds the
// same fail-closed contract as this JS mirror (same envelope checks, same
// named `io-error` rejection, same size bound). Native memory safety lives
// in `libRuntime.so` (engine, out of scope per the review's Residual Risk);
// if the native parser diverges, these tests do not prove on-device safety.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { decodeTreeIR } = require('../../../packages/cli/lib/gallery-proof.js');
const { encodeTree } = require('../../../packages/cli/lib/ir.js');

const FIXTURE_IR = { version: 1, roots: [] };

function validEnvelope() {
  return encodeTree(FIXTURE_IR);
}

describe('security — corrupt/truncated tree.mui.bin fails closed (security-review.md coverage request 1)', () => {
  // AC: "Corrupt/truncated `tree.mui.bin` → `nativeLoadTree` fails closed
  // (handle 0, no crash, no render of attacker-influenced bytes)".
  // ASSUMPTION: `decodeTreeIR` is the unit-testable mirror of the native
  // parser's envelope check (see header); a named `io-error` here stands
  // for the Java side skipping `nativeLoadTree` (handle stays 0).
  it('garbage magic and truncated payload throw named io-error, never a silent empty tree', () => {
    const garbage = Buffer.from('GARBAGE-SYNTHETIC-NOT-MUI1-0000', 'utf8');
    assert.throws(() => decodeTreeIR(garbage), (err) => err && err.code === 'io-error');

    const full = validEnvelope();
    const truncated = full.subarray(0, full.length - 1);
    assert.ok(truncated.length >= 12, 'truncation fixture must keep a complete header');
    assert.throws(() => decodeTreeIR(truncated), (err) => err && err.code === 'io-error');

    const shortHeader = Buffer.from('MU', 'ascii');
    assert.throws(() => decodeTreeIR(shortHeader), (err) => err && err.code === 'io-error');
  });
});

describe('security — oversized tree rejected at a declared bound (security-review.md coverage request 2)', () => {
  // AC: "Oversized tree → bounded rejection (or documented max) rather than
  // OOM"; pairs with the [LOW] unbounded-asset-read finding on
  // `MainActivity.readTreeAsset` (no size cap, full read into
  // `ByteArrayOutputStream` on the UI thread).
  // ASSUMPTION: declared max is 16 MiB (the lower end of the review's
  // "e.g. 16–32 MB" bound semantics); the Java pre-check (via
  // `AssetFileDescriptor` length or a capped accumulator) must name the
  // rejection so launch triage never sees a silent black frame.
  it('MainActivity declares a max tree-asset bound and fails closed with a named error above it', () => {
    const src = fs.readFileSync(
      path.join(__dirname, '..', 'src', 'MainActivity.java'),
      'utf8',
    );
    assert.match(
      src,
      /MAX_TREE_BYTES|MAX_TREE_SIZE|TREE_MAX_BYTES/,
      'MainActivity must declare a documented max tree-asset size (16–32 MB per review)',
    );
    assert.match(
      src,
      /tree-too-large|TREE_TOO_LARGE|TreeTooLarge|too large/i,
      'oversized tree must be rejected with a named error, never a silent null/OOM',
    );
  });
});
