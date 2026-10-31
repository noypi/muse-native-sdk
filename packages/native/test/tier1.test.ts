/**
 * TC-M3-001 — the 10 tier-1 primitives construct from TSX with manifest
 * prop schemas (FR-M3-001, AC-M3-001, RULE-M3-001).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the components
 * through the package public entry (index.js), reads muse.manifest.json and
 * index.d.ts from disk. No network, no secrets, no services.
 *
 * ASSUMPTION P1 (runtime shape): each primitive is a function taking an
 * optional props record and returning a `{ type, props }` descriptor, so
 * existing consumers do not rename. Unknown prop keys are NOT rejected at
 * runtime; accepted-key equality with the manifest schema is enforced at
 * the type layer (index.d.ts per-component prop types).
 * ASSUMPTION P2 (type-layer check): index.d.ts declares one Props type per
 * primitive (e.g. SurfaceProps) generated from the T1 manifest schemas.
 * Verified as source text so the test stays independent of the TS compiler.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const { components } = require('../index.js');
const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);
const DTS_URL = new URL('../index.d.ts', import.meta.url);

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

function loadManifest() {
  return JSON.parse(readFileSync(MANIFEST_URL, 'utf8'));
}

// Representative schema props per primitive (keys must exist in the manifest).
const SCHEMA_PROPS = {
  Surface: { tone: 'surface', elevation: 1 },
  Text: { variant: 'body-medium', color: 'on-surface' },
  Icon: { name: 'star', size: 24, color: 'primary' },
  Image: { source: 'https://example.test/img.png', contentFit: 'cover' },
  Ripple: { color: 'primary', bounded: true },
  Divider: { orientation: 'horizontal', color: 'outline' },
  Scroll: { orientation: 'vertical' },
  Stack: { direction: 'row', spacing: 8 },
  TextFieldEditCore: { value: 'hello', placeholder: 'type', enabled: true },
  OverlayHost: {},
};

// ---------------------------------------------------------------------------
// AC-M3-001: each of the 10 primitives constructs with its schema props
// ---------------------------------------------------------------------------

test('all 10 tier-1 primitives are exported', () => {
  for (const name of TIER_1) {
    assert.equal(typeof components[name], 'function', `primitive not exported: ${name}`);
  }
});

for (const name of TIER_1) {
  test(`${name} constructs a descriptor from schema props`, () => {
    const descriptor = components[name](SCHEMA_PROPS[name]);
    assert.equal(descriptor.type, name, `${name} descriptor type must equal its name`);
    assert.deepEqual(descriptor.props, SCHEMA_PROPS[name]);
  });
}

test('primitives construct with no props (props default to {})', () => {
  for (const name of TIER_1) {
    const descriptor = components[name]();
    assert.equal(descriptor.type, name);
    assert.deepEqual(descriptor.props, {});
  }
});

// ---------------------------------------------------------------------------
// Boundary: accepted prop keys equal the manifest schema (ASSUMPTION P1)
// ---------------------------------------------------------------------------

test('schema prop keys used here exist in the manifest schemas', () => {
  const manifest = loadManifest();
  const byName = new Map(manifest.components.map((c) => [c.name, c]));
  for (const name of TIER_1) {
    const schemaKeys = new Set(Object.keys(byName.get(name).props ?? {}));
    for (const key of Object.keys(SCHEMA_PROPS[name])) {
      assert.ok(schemaKeys.has(key), `${name}.${key} is not in the manifest schema`);
    }
  }
});

test('index.d.ts declares one Props type per tier-1 primitive', () => {
  const dts = readFileSync(DTS_URL, 'utf8');
  for (const name of TIER_1) {
    assert.ok(
      dts.includes(`${name}Props`),
      `index.d.ts must declare per-component prop types generated from the manifest (${name}Props missing)`,
    );
  }
});

// ---------------------------------------------------------------------------
// Error case: unknown primitive name is not exported
// ---------------------------------------------------------------------------

test('an unknown primitive name is not exported', () => {
  assert.equal(components.Foo, undefined);
  assert.equal(components.View, undefined);
});

test('accessing an unknown primitive cannot construct a descriptor', () => {
  assert.throws(() => {
    components.NotAPrimitive({});
  }, TypeError);
});

// ---------------------------------------------------------------------------
// T4 / TC-M3-002: token-only painting + shared Ripple/state layer
// (FR-M3-001, AC-M3-002, RULE-M3-002).
//
// ASSUMPTION S1 (StateLayerSpec shape): `driveState(target, state)` returns a
// plain spec object that echoes `{ target, state }` and references the shared
// Ripple layer (the string "Ripple" appears in its JSON form, e.g. as a
// nested descriptor type or layer name). The spec carries token-role
// references only — no literal colour/dimension.
// ASSUMPTION S2 (interactive set): the tier-1 interactive primitives are
// Surface, Ripple, and TextFieldEditCore. Each routes state transitions
// through `driveState`: calling the primitive with a `state` prop yields a
// descriptor whose `props.stateLayer` deep-equals `driveState(name, state)`.
// ASSUMPTION S3 (literal patterns): a literal colour is a hex code
// (`#rgb`/`#rrggbb`/`#rrggbbaa`) or an `rgb()/rgba()/hsl()/hsla()` call; a
// literal dimension is a number with a `px`/`dp`/`sp`/`pt` unit. Token-role
// references are plain strings such as "primary" or "on-surface".
// ASSUMPTION G3 (gate testability): `scripts/check-tokens.js` resolves the
// scanned library root from the `LIB_PATH` env var when set, defaulting to
// the package root next to the script (mirrors ASSUMPTION G1 of
// check-manifest.js), and supports `--scope lib`.
// ---------------------------------------------------------------------------

const T4_STATES = ['hover', 'focus', 'pressed', 'disabled'];
const T4_INTERACTIVE = ['Surface', 'Ripple', 'TextFieldEditCore'];
const HEX_LITERAL = /#[0-9a-fA-F]{3,8}\b/;
const FN_COLOR_LITERAL = /\b(?:rgb|rgba|hsl|hsla)\s*\(/;
const DIMENSION_LITERAL = /\b\d+(?:\.\d+)?(?:px|dp|sp|pt)\b/;

function t4HasLiteral(text) {
  return (
    HEX_LITERAL.test(text) || FN_COLOR_LITERAL.test(text) || DIMENSION_LITERAL.test(text)
  );
}

function loadIndexSource() {
  return readFileSync(new URL('../index.js', import.meta.url), 'utf8');
}

test('driveState is exported from the package entry', () => {
  const entry = require('../index.js');
  assert.equal(
    typeof entry.driveState,
    'function',
    'driveState(target, state) must be exported from the package entry',
  );
});

for (const state of T4_STATES) {
  test(`driveState returns a Ripple-backed StateLayerSpec for state "${state}"`, () => {
    const { driveState } = require('../index.js');
    const spec = driveState('Surface', state);
    assert.equal(typeof spec, 'object', 'StateLayerSpec must be an object');
    assert.equal(spec.state, state, 'StateLayerSpec must echo the requested state');
    assert.equal(spec.target, 'Surface', 'StateLayerSpec must echo the target primitive');
    assert.ok(
      JSON.stringify(spec).includes('Ripple'),
      `StateLayerSpec for "${state}" must route through the shared Ripple/state layer`,
    );
  });
}

test('driveState specs carry token-role references only (no literal colour/dimension)', () => {
  const { driveState } = require('../index.js');
  for (const name of T4_INTERACTIVE) {
    for (const state of T4_STATES) {
      const spec = driveState(name, state);
      assert.ok(
        !t4HasLiteral(JSON.stringify(spec)),
        `driveState("${name}", "${state}") must not contain a literal colour/dimension`,
      );
    }
  }
});

test('library component defaults contain no literal colour/dimension', () => {
  const source = loadIndexSource();
  assert.ok(
    !t4HasLiteral(source),
    'packages/native library code must reference token roles only (no literal colour/dimension defaults)',
  );
});

// BUG-M3-FIDELITY-001 T2: withStateLayer numeric output asserted with
// INDEPENDENT literals (never driveState output — the old deepEqual against
// driveState() was self-referential and passed with symbolic strings).
// withStateLayer is internal (not exported); it is exercised through the
// public component boundary, which is its only caller surface.
const T2_NUMERIC_OPACITY: Record<string, number> = {
  hover: 0.08,
  focus: 0.12,
  pressed: 0.16,
};

for (const name of T4_INTERACTIVE) {
  test(`${name} routes hover/focus/pressed output through the shared state layer with numeric M3 opacity`, () => {
    const { components: tier1Components } = require('../index.js');
    for (const state of ['hover', 'focus', 'pressed'] as const) {
      const descriptor = tier1Components[name]({ state });
      // Independent literal — not driveState(name, state).
      assert.deepEqual(
        descriptor.props.stateLayer,
        {
          target: name,
          state,
          layer: {
            type: 'Ripple',
            props: { color: 'primary', opacity: T2_NUMERIC_OPACITY[state] },
          },
        },
        `${name} output for state "${state}" must carry the numeric M3 opacity`,
      );
      assert.equal(typeof descriptor.props.stateLayer.layer.props.opacity, 'number');
    }
  });

  test(`${name} yields a numeric (baseline-defined) disabled opacity`, () => {
    // ASSUMPTION D1 (shared with T1): the exact disabled numeric lives in the
    // conformance baseline; this pins numeric-type + finite range only.
    const { components: tier1Components } = require('../index.js');
    const descriptor = tier1Components[name]({ state: 'disabled' });
    const opacity = descriptor.props.stateLayer.layer.props.opacity;
    assert.equal(typeof opacity, 'number');
    assert.ok(Number.isFinite(opacity));
    assert.ok(opacity >= 0 && opacity <= 1);
  });

  test(`${name} stateLayer output carries no literal colour/dimension`, () => {
    const { components: tier1Components } = require('../index.js');
    for (const state of T4_STATES) {
      const descriptor = tier1Components[name]({ state });
      assert.ok(
        !t4HasLiteral(JSON.stringify(descriptor.props.stateLayer)),
        `${name} stateLayer for "${state}" must be token-role only`,
      );
    }
  });

  test(`${name} returns props untouched when state is absent or undefined`, () => {
    // withStateLayer absent/undefined-state path via the public boundary.
    const { components: tier1Components } = require('../index.js');
    for (const props of [{ tone: 'surface' }, { state: undefined }, undefined]) {
      const descriptor = tier1Components[name](props);
      assert.ok(descriptor.props && !('stateLayer' in descriptor.props));
    }
  });

  test(`${name} propagates driveState errors with no new error type`, () => {
    const { components: tier1Components } = require('../index.js');
    assert.throws(() => tier1Components[name]({ state: 42 }), TypeError);
    assert.throws(() => tier1Components[name]({ state: 'giant' }), RangeError);
  });
}

test('check-tokens.js --scope lib exits 0 on the clean library tree', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const scriptPath = fileURLToPath(new URL('../scripts/check-tokens.js', import.meta.url));
  const out = execFileSync(process.execPath, [scriptPath, '--scope', 'lib'], {
    encoding: 'utf8',
  });
  assert.ok(out.includes('OK'), 'token gate must report OK on the clean tree');
});

test('check-tokens.js --scope lib exits 1 naming file:line for a smuggled literal', () => {
  const { execFileSync } = require('node:child_process');
  const { fileURLToPath } = require('node:url');
  const fs = require('node:fs');
  const os = require('node:os');
  const path = require('node:path');
  const scriptPath = fileURLToPath(new URL('../scripts/check-tokens.js', import.meta.url));
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-lib-'));
  try {
    fs.writeFileSync(
      path.join(dir, 'smuggled.js'),
      'module.exports = { color: "#ff0000", margin: "8px" };\n',
    );
    let err = null;
    try {
      execFileSync(process.execPath, [scriptPath, '--scope', 'lib'], {
        encoding: 'utf8',
        env: { ...process.env, LIB_PATH: dir },
      });
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'token gate must fail when a literal is smuggled into library code');
    assert.equal(err.status, 1, 'token gate must exit 1 on a smuggled literal');
    assert.ok(
      /smuggled\.js:\d+/.test(String(err.stdout) + String(err.stderr)),
      'token gate must list file:line of the smuggled literal',
    );
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

// ---------------------------------------------------------------------------
// Stage 3 fix follow-up (MINOR review): driveState throws on unknown state.
// Non-string state → TypeError; unknown string → RangeError.
// ---------------------------------------------------------------------------

test('driveState throws RangeError on unknown string state', () => {
  const { driveState } = require('../index.js');
  assert.throws(() => {
    driveState('Surface', 'bogus');
  }, RangeError);
});

test('driveState throws TypeError on non-string state', () => {
  const { driveState } = require('../index.js');
  assert.throws(() => {
    driveState('Surface', 42);
  }, TypeError);
});

