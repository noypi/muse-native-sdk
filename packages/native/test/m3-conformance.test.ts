/**
 * TC-M3-019 — FR-M3-008 conformance proof: every tier-1/tier-2 component has
 * a normative M3 conformance row pinned to the recorded baseline
 * (FR-M3-008, AC-M3-017/018/019).
 *
 * Hermetic: node:test + node:assert/strict only. Imports `driveState`
 * through the package public entry (index.js), reads
 * `engine/layout/m3_matrix.json` (the TC-M3-018 baseline file) from disk.
 * No network, no secrets, no services, no live device.
 *
 * What this file proves (and what it does not):
 * - Row existence + baseline reference + required conformance fields are
 *   asserted against the recorded baseline file (TC-M3-018/019).
 * - Numeric opacities are asserted as INDEPENDENT literals (0.08 / 0.12 /
 *   0.16 / 0.38) — never `driveState` output echoed against itself (the
 *   T4 self-referential defect in tier1.test.ts).
 * - Token-role-only output is asserted on the resolved descriptor (no
 *   hex/rgb/literal-dimension in the resolved JSON), and a smuggled literal
 *   is shown to trip the same regex — the unit-level shadow of the
 *   `npm run test:tokens -w @muse/native` device-token gate (TC-M3-020),
 *   which runs against device-resolved tokens, not descriptor echo.
 *
 * ASSUMPTION A1 (plan A1): the disabled state-layer opacity value lives in
 * the conformance baseline, not the AC text. The baseline records no
 * numeric yet, so the test pins the documented M3 disabled state-layer
 * value 0.38 until T3 pins the exact row.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { driveState } = require('../index.js');

const MATRIX_URL = new URL(
  '../../../engine/layout/m3_matrix.json',
  import.meta.url,
);

function loadMatrix() {
  return JSON.parse(readFileSync(MATRIX_URL, 'utf8'));
}

// ---------------------------------------------------------------------------
// AC-M3-017 (TC-M3-018): the baseline record is named once and every row
// references it.
// ---------------------------------------------------------------------------

test('baseline record names the M3 source revision and date (AC-M3-017)', () => {
  const matrix = loadMatrix();
  assert.equal(typeof matrix.schemaVersion, 'number');
  assert.equal(typeof matrix.tokenSnapshot, 'string');
  assert.ok(
    /^\d{4}-\d{2}-\d{2}$/.test(matrix.tokenSnapshot),
    `tokenSnapshot must be a recorded revision date, got: ${matrix.tokenSnapshot}`,
  );
});

test('every component row references the recorded baseline (AC-M3-017)', () => {
  const matrix = loadMatrix();
  const ref = String(matrix.tokenSnapshot);
  for (const row of matrix.components) {
    assert.ok(
      typeof row.baseline === 'string' && row.baseline.includes(ref),
      `${row.name}: row must reference the baseline revision ${ref}`,
    );
  }
});

// ---------------------------------------------------------------------------
// AC-M3-018 (TC-M3-019): every tier-1 and tier-2 component has a row.
// Names are the spec tier tables (specs-m3-native.md Component Tiers).
// ---------------------------------------------------------------------------

const TIER_1 = [
  'Surface',
  'Text',
  'Icon',
  'Image',
  'Ripple',
  'Divider',
  'Scroll',
  'Stack',
  'TextFieldEditCore',
  'OverlayHost',
];

const TIER_2 = [
  'Button',
  'IconButton',
  'FAB',
  'ExtendedFab',
  'Card',
  'Chip',
  'Checkbox',
  'Radio',
  'Switch',
  'Slider',
  'Badge',
  'LinearProgress',
  'CircularProgress',
  'ListItem',
  'Avatar',
  'Tooltip',
];

for (const name of [...TIER_1, ...TIER_2]) {
  test(`conformance row exists for tier component ${name} (AC-M3-018)`, () => {
    const matrix = loadMatrix();
    const row = matrix.components.find((c) => c.name === name);
    assert.ok(row, `${name}: no conformance row — a component with no row fails the gallery check`);
  });
}

// ---------------------------------------------------------------------------
// AC-M3-018 (TC-M3-019): every row names container/label tokens, state
// opacities, shape, elevation, typescale, keyboard/focus, motion token,
// a11y role.
// ---------------------------------------------------------------------------

const REQUIRED_ROW_FIELDS = [
  'containerToken',
  'labelToken',
  'stateOpacities',
  'shape',
  'elevation',
  'typescale',
  'keyboardFocus',
  'motionToken',
  'a11yRole',
];

for (const name of [...TIER_1, ...TIER_2]) {
  test(`conformance row for ${name} names all required fields (AC-M3-018)`, () => {
    const matrix = loadMatrix();
    const row = matrix.components.find((c) => c.name === name);
    assert.ok(row, `${name}: no conformance row`);
    for (const field of REQUIRED_ROW_FIELDS) {
      assert.ok(
        row[field] !== undefined && row[field] !== null && row[field] !== '',
        `${name}: row is missing required field '${field}'`,
      );
    }
  });

  test(`conformance row for ${name} pins hover/focus/press opacities 8/12/16 (AC-M3-018)`, () => {
    const matrix = loadMatrix();
    const row = matrix.components.find((c) => c.name === name);
    assert.ok(row, `${name}: no conformance row`);
    assert.equal(row.stateOpacities?.hover, 0.08, `${name}: hover opacity must be 0.08`);
    assert.equal(row.stateOpacities?.focus, 0.12, `${name}: focus opacity must be 0.12`);
    assert.equal(row.stateOpacities?.pressed, 0.16, `${name}: pressed opacity must be 0.16`);
  });
}

// ---------------------------------------------------------------------------
// AC-M3-018 numeric proof on the shared path: driveState resolves numeric
// M3 opacities. Independent literals — never driveState echoed against
// itself. ASSUMPTION A1: disabled pins 0.38 until the baseline pins it.
// ---------------------------------------------------------------------------

test('driveState hover resolves numeric 0.08 (AC-M3-018)', () => {
  const out = driveState('Surface', 'hover');
  assert.equal(typeof out.layer.props.opacity, 'number');
  assert.equal(out.layer.props.opacity, 0.08);
});

test('driveState focus resolves numeric 0.12 (AC-M3-018)', () => {
  const out = driveState('Surface', 'focus');
  assert.equal(typeof out.layer.props.opacity, 'number');
  assert.equal(out.layer.props.opacity, 0.12);
});

test('driveState pressed resolves numeric 0.16 (AC-M3-018)', () => {
  const out = driveState('Surface', 'pressed');
  assert.equal(typeof out.layer.props.opacity, 'number');
  assert.equal(out.layer.props.opacity, 0.16);
});

test('driveState disabled resolves numeric baseline value 0.38 (AC-M3-018, ASSUMPTION A1)', () => {
  const out = driveState('Surface', 'disabled');
  assert.equal(typeof out.layer.props.opacity, 'number');
  assert.equal(out.layer.props.opacity, 0.38);
});

// ---------------------------------------------------------------------------
// AC-M3-019 (TC-M3-020): no literal style value — resolved output is
// token-role only. Unit-level shadow of the device-token gate
// (`npm run test:tokens -w @muse/native` → exit 0); the gate itself runs
// against device-resolved tokens, not descriptor echo.
// ---------------------------------------------------------------------------

const LITERAL_RE = /#[0-9a-fA-F]{3,8}\b|rgba?\(|\b\d+(?:\.\d+)?(?:px|dp|sp|pt)\b/;

test('driveState resolved output carries no literal colour/dimension (AC-M3-019)', () => {
  for (const state of ['hover', 'focus', 'pressed', 'disabled']) {
    const out = driveState('Surface', state);
    const json = JSON.stringify(out);
    assert.ok(
      !LITERAL_RE.test(json),
      `state ${state}: resolved output must be token-role only, got literal in ${json}`,
    );
  }
});

test('literal detector trips on a smuggled literal (AC-M3-019 gate is live)', () => {
  const smuggled = JSON.stringify({
    target: 'Surface',
    state: 'hover',
    layer: { type: 'Ripple', props: { color: '#ff0000', opacity: 0.08 } },
  });
  assert.ok(
    LITERAL_RE.test(smuggled),
    'the literal detector must flag a smuggled hex colour — otherwise the gate is blind',
  );
});
