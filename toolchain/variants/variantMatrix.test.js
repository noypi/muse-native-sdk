'use strict';
// T13 — variant matrix config gate (NFR-BUILD-018 / AC-BUILD-070).
// Hermetic: node:test + node:assert/strict + stdlib (fs/path) only. Reads the
// checked-in declarative config at ../variant-matrix.json relatively — no
// network, no secrets, no live services. This test file is owned by the
// code-unit-tester; the JSON itself is the coder's claim — this file never
// writes it. Red state: ../variant-matrix.json does not exist yet, so every
// case below fails (ENOENT / MODULE_NOT_FOUND) until T13 lands.
//
// Interface under test (plan verbatim):
//   none — declarative config; test asserts schema: { variants: [{ name: string, runtime: string, featureFlags: Record<string, boolean> }] } with release variants defaulting every flag to the safe value (false)

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const MATRIX_PATH = path.join(__dirname, 'variant-matrix.json');

function loadMatrix() {
  const text = fs.readFileSync(MATRIX_PATH, 'utf8');
  return JSON.parse(text);
}

function releaseVariants(matrix) {
  return matrix.variants.filter((v) => /release|production/i.test(v.name));
}

describe('T13 — variant-matrix.json schema (AC-BUILD-070)', () => {
  it('declares a variants array', () => {
    const matrix = loadMatrix();
    assert.ok(Array.isArray(matrix.variants), 'matrix must declare a variants array');
    assert.ok(matrix.variants.length > 0, 'variants must not be empty');
  });

  it('every variant carries a name, a runtime, and a featureFlags record', () => {
    const matrix = loadMatrix();
    for (const variant of matrix.variants) {
      assert.equal(typeof variant.name, 'string', `variant name must be a string: ${JSON.stringify(variant)}`);
      assert.equal(typeof variant.runtime, 'string', `variant ${variant.name} must declare a runtime`);
      assert.ok(
        variant.featureFlags && typeof variant.featureFlags === 'object',
        `variant ${variant.name} must declare a featureFlags record`
      );
      for (const [flag, value] of Object.entries(variant.featureFlags)) {
        assert.equal(
          typeof value,
          'boolean',
          `variant ${variant.name} flag ${flag} must default to a boolean, got: ${JSON.stringify(value)}`
        );
      }
    }
  });
});

describe('T13 — required variant rows (AC-BUILD-070)', () => {
  it('covers dev, staging, and production rows', () => {
    const matrix = loadMatrix();
    const names = matrix.variants.map((v) => v.name.toLowerCase());
    for (const row of ['dev', 'staging', 'production']) {
      assert.ok(
        names.some((n) => n.includes(row)),
        `matrix must include a ${row} row, got: ${JSON.stringify(names)}`
      );
    }
  });

  it('covers a QuickJS variant row', () => {
    const matrix = loadMatrix();
    const hasQuickjs = matrix.variants.some(
      (v) => /quickjs/i.test(v.name) || /quickjs/i.test(v.runtime)
    );
    assert.ok(
      hasQuickjs,
      `matrix must include a QuickJS variant row, got: ${JSON.stringify(matrix.variants.map((v) => v.name))}`
    );
  });
});

describe('T13 — release variants default every flag to the safe value (AC-BUILD-070)', () => {
  it('every release/production variant defaults every feature flag to false', () => {
    // ASSUMPTION: the safe default for every feature flag is false; a release
    // variant with an unsafe (true) default fails this gate.
    const matrix = loadMatrix();
    const releases = releaseVariants(matrix);
    assert.ok(releases.length > 0, 'matrix must include at least one release variant');
    for (const variant of releases) {
      for (const [flag, value] of Object.entries(variant.featureFlags)) {
        assert.equal(
          value,
          false,
          `release variant ${variant.name} flag ${flag} must default to false (safe)`
        );
      }
    }
  });
});
