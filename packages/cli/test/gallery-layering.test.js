'use strict';
// Batch 2 IME-inset-safe sheet — FR-M3-010 / AC-M3-022 IME-inset-safe
// clause (checklist L4). Hermetic halves of the L4 contract, owned here
// per the plan Batch 2 row (`packages/cli/test/gallery-layering.test.js`
// hermetic halves); the device frame-capture proof belongs to L6/tester.
//
// Interface (plan L4 verbatim):
//   `BottomSheetProps { open: boolean; imeSafe: true; insetSource:
//   'FR-LAYOUT-012' }` — when `open && imeSafe`, the sheet consumes the
//   layout-owned animated inset per frame; any other `insetSource` value
//   fails the build. `none` beyond the prop contract — geometry proof is
//   frame captures, not a new callable.
//
// File choice: the two relocated `imeSafe` hermetic assertions (manifest
// schema + composite echo, recorded in the Batch 1 L4 stub) assert CLI- and
// gate-visible surface, and the Batch 2 plan row assigns hermetic halves to
// this CLI test file — so they are restored HERE, not by editing Batch 1's
// `packages/native/test/layering.test.ts` (which stays byte-identical).
// Hermetic: node:test + node:assert/strict + stdlib only. Reads committed
// sources from disk; no network, no secrets, no services, no device.
// ASSUMPTION: `insetSource` is pinned as the literal type `'FR-LAYOUT-012'`
//   so any other value is a build-time type error (the plan's "fails the
//   build"); the coder owns the exact gate wiring, this file pins the
//   literal contract.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const CLI_DIR = path.resolve(__dirname, '..');
const CHECKOUT_ROOT = path.join(CLI_DIR, '..', '..');
const NATIVE_ROOT = path.join(CHECKOUT_ROOT, 'packages', 'native');
const MANIFEST_PATH = path.join(NATIVE_ROOT, 'muse.manifest.json');
const BOTTOM_SHEET_PATH = path.join(NATIVE_ROOT, 'composites', 'BottomSheet.tsx');
const PRESENTER_PATH = path.join(
  CHECKOUT_ROOT, 'engine', 'runtime', 'src', 'gallery_present.cpp',
);

function read(p) {
  return fs.readFileSync(p, 'utf8');
}

describe('L4 — IME-inset-safe sheet prop contract (AC-M3-022)', () => {
  it('BottomSheet manifest schema exposes the imeSafe prop contract', () => {
    const manifest = JSON.parse(read(MANIFEST_PATH));
    const entry = Array.isArray(manifest.components)
      ? manifest.components.find((c) => c && c.name === 'BottomSheet')
      : (manifest.components?.BottomSheet ?? manifest.BottomSheet);
    assert.ok(entry, 'muse.manifest.json must declare a BottomSheet component entry');
    assert.equal(
      entry.props?.imeSafe?.type,
      'boolean',
      'BottomSheet manifest entry must declare `imeSafe: boolean`',
    );
  });

  it('BottomSheet composite echoes the imeSafe prop', () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      src.includes('imeSafe'),
      'composites/BottomSheet.tsx must echo the `imeSafe` prop',
    );
  });

  it("BottomSheetProps pins insetSource to 'FR-LAYOUT-012'", () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      src.includes('FR-LAYOUT-012'),
      "BottomSheetProps must name the layout-owned inset source 'FR-LAYOUT-012'",
    );
    assert.ok(
      src.includes('imeSafe') && src.includes('open'),
      'BottomSheetProps must carry { open, imeSafe } for the open && imeSafe gate',
    );
  });

  it('open && imeSafe consumes the animated inset per frame (no gallery copy)', () => {
    const presenter = read(PRESENTER_PATH);
    const composite = read(BOTTOM_SHEET_PATH);
    assert.ok(
      presenter.includes('anchorSheetToImeInset'),
      'engine must bind sheet Y via anchorSheetToImeInset per frame (FR-LAYOUT-012 consumption)',
    );
    assert.ok(
      composite.includes('insetSource'),
      'BottomSheet must declare the insetSource contract so open && imeSafe gates layout-owned inset consumption',
    );
    assert.ok(
      !composite.includes('sheetY') && !composite.includes('anchorSheet'),
      'BottomSheet composite must not copy inset into gallery state — consumption only, no geometry math here',
    );
  });

  it("any other insetSource value fails the build", () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      src.includes("insetSource: 'FR-LAYOUT-012'") ||
        src.includes('insetSource: "FR-LAYOUT-012"'),
      "insetSource must be the literal type 'FR-LAYOUT-012' so any other value is a build-time type error",
    );
  });

  it('geometry proof stays frame captures — no new callable beyond the prop contract', () => {
    const src = read(BOTTOM_SHEET_PATH);
    assert.ok(
      !src.includes('getSheetGeometry') && !src.includes('computeSheetY'),
      'BottomSheet must expose no geometry callable — frame-capture proof only (L6/tester)',
    );
  });
});
