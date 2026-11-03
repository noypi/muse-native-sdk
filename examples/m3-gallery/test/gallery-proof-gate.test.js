'use strict';

// Batch 1 (T1 + T2) — proof-gate relocation contract (FR-SAMPLE-017).
//
// Binds ONLY to the plan Batch 1 interfaces (reproduced verbatim in
// unit-tests/T1.md and unit-tests/T2.md):
//   T1: `evaluateProofGate(descriptor: object): { proofGate: 'pass'|'fail' }`
//       (now from `packages/cli/lib/gallery-proof.js`, not `src/`).
//   T2: none — build-flag guard, no new callable surface.
// Hermetic: node:test + node:assert/strict + node:fs/node:path only.
// Reads shipped-source text and requires the two modules; no network, no
// secrets, no application config, no device, no live service, no
// `process.env` read, no shared mutable state.
//
// RED STATE (pre-coder): every gate-location assertion below fails because
// the three helpers still live in shipped `src/` and no `__DEV__` token
// exists repo-wide. That red is the expected contract pin, not a test bug.
//
// ASSUMPTIONS (plan Interface genuinely silent — coder reconciles per
// GR-08 T1.9; behavior pinned, spelling reconcilable):
//   A1 (gate shape): the plan pins `{ proofGate: 'pass'|'fail' }`. The
//       current `src/` helper returns `{ pass: boolean }` — a different
//       shape. Tests pin the PLAN shape; the coder implements it in CLI.
//   A2 (`__DEV__` define): zero `__DEV__` tokens exist repo-wide, so no
//       gallery build-flag pattern could be verified. The test pins only
//       TC-049 presence (`__DEV__` in `gallerySurface.js`); the coder owns
//       the define mechanism consistent with the gallery build. If no
//       consistent pattern exists, T2 implementation stops per T1.3.
//   A3 (kept exports): the plan says `src/` keeps ONLY
//       `renderGallerySection` + `attachGallerySurface`. Tests assert those
//       two exist and the three gate names are gone; any further export
//       pruning (e.g. `BuildError`, `THEMES`) is the coder's call.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const SRC_PATH = join(__dirname, '..', 'src', 'gallerySurface.js');
const CLI_PATH = join(__dirname, '..', '..', '..', 'packages', 'cli', 'lib', 'gallery-proof.js');
const GATE_NAMES = ['evaluateProofGate', 'isStaticFallbackEnabled', 'renderStaticFallback'];

function readText(path) {
  return readFileSync(path, 'utf8');
}

describe('T1 — gate helpers are out of shipped src (TC-SAMPLE-048)', () => {
  it('negated grep: no gate-helper name appears in gallerySurface.js', () => {
    const src = readText(SRC_PATH);
    for (const name of GATE_NAMES) {
      assert.ok(
        !src.includes(name),
        `shipped src must not contain gate helper ${name} (AC-SAMPLE-047)`,
      );
    }
  });

  it('src keeps renderGallerySection + attachGallerySurface', () => {
    const surface = require(SRC_PATH);
    assert.equal(typeof surface.renderGallerySection, 'function', 'src must export renderGallerySection()');
    assert.equal(typeof surface.attachGallerySurface, 'function', 'src must export attachGallerySurface()');
  });

  it('src does not export the gate helpers (no alias re-export)', () => {
    const surface = require(SRC_PATH);
    for (const name of GATE_NAMES) {
      assert.equal(
        surface[name],
        undefined,
        `src must not export ${name} — relocation, not aliasing`,
      );
    }
  });

  it('gate lives in packages/cli/lib/gallery-proof.js as evaluateProofGate', () => {
    const proof = require(CLI_PATH);
    assert.equal(
      typeof proof.evaluateProofGate,
      'function',
      'CLI gallery-proof.js must export evaluateProofGate(descriptor) (plan T1 Interface)',
    );
  });

  it('CLI evaluateProofGate returns { proofGate: pass|fail } and fails closed', () => {
    const proof = require(CLI_PATH);
    assert.equal(typeof proof.evaluateProofGate, 'function', 'gate helper must exist first (A1)');
    assert.deepEqual(
      proof.evaluateProofGate({ staticFallback: false }),
      { proofGate: 'pass' },
      'default tree must pass the gate',
    );
    for (const bad of [{ staticFallback: true }, { staticFallback: 1 }, { staticFallback: 'yes' }]) {
      assert.deepEqual(
        proof.evaluateProofGate(bad),
        { proofGate: 'fail' },
        `truthy staticFallback ${JSON.stringify(bad)} must fail the gate`,
      );
    }
  });
});

describe('T2 — remaining in-app diagnostic is guarded with __DEV__ (TC-SAMPLE-049)', () => {
  it('__DEV__ is present in gallerySurface.js', () => {
    const src = readText(SRC_PATH);
    assert.ok(
      src.includes('__DEV__'),
      'any diagnostic fallback that must remain in-app must be guarded with __DEV__ (AC-SAMPLE-048)',
    );
  });
});
