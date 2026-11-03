'use strict';
// T2 — demo kind-map emits only gate-known kinds (fallback-kinds Batch 1).
// Hermetic: node:test + node:assert/strict only. In-memory stub route +
// inline demo descriptors; no device, no downloads, no secrets, no config,
// no host-env mutation.
//
// Binds ONLY to the settled plan Interface:
//   `mapDemoKind(demoKind: string): string` — `lens→Image`,
//   `field→TextInput`, `list→Card`, `ErrorSnackbar→SnackbarHost`;
//   every other demo/unknown kind fails closed NAMED
//   (`unknown demo kind '<kind>'`, never silent, never substituted).
// ASSUMPTIONS: CommonJS at `../src/demoNav.js` exporting `demoCompFor`
// (the shipped seam, aliased here as mapDemoKind); unknown input throws
// `Error('unknown demo kind \'<kind>\'')` (fail closed named, per the
// approved mapping — the plan's `Interface:` line lists the four exact
// pairs; the fail-closed tail follows T1's no-substitution rule).
// Spec refs: AC-SAMPLE-023.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const MAP_PATH = '../src/demoNav.js';

// Exact per-item contract mapping: demo descriptor -> generic target.
const EXACT_MAP = [
  ['lens', 'Image'],
  ['field', 'TextInput'],
  ['list', 'Card'],
  ['ErrorSnackbar', 'SnackbarHost'],
];

// Demo/unknown kinds that must fail closed NAMED (never resolve, never
// substitute). Includes the T1 engine-gate probes that reach this seam.
const FAIL_CLOSED_KINDS = [
  'guessMask',
  'DemoState',
  'AnimatableProp',
  'string',
  'unknown',
  'Field',
  'InlineError',
  '',
];

// Gate-known stub route: the in-memory stand-in for the engine gate
// (`IsKnownGalleryComponent` in engine/runtime/src/gallery_present.cpp,
// the Real adapter). A mapped descriptor resolves iff the stub route
// accepts the mapped comp. The stub set mirrors the retained gate
// vocabulary; the engine T1 pin owns the Real.
const STUB_KNOWN = new Set([
  'Button', 'ExtendedFab', 'Chip', 'Checkbox', 'Switch', 'Slider',
  'LinearProgress', 'IconButton', 'Fab', 'Dialog', 'Menu', 'Tooltip',
  'Icon', 'Avatar', 'Image', 'CircularProgress', 'Radio', 'Badge',
  'TextFieldEditCore', 'TextInput', 'SearchBar', 'Carousel', 'Card',
  'Divider', 'BottomSheet', 'NavigationBar', 'SegmentedButton',
  'ButtonGroup', 'SplitButton', 'NavigationRail', 'NavigationDrawer',
  'BottomAppBar', 'SnackbarHost', 'GalleryRow', 'Surface', 'Ripple',
  'TopAppBar',
]);

// Kinds the map must never emit (demo-specific + serializer type names).
const FORBIDDEN_EMISSIONS = new Set([
  'lens', 'field', 'list', 'string', 'ErrorSnackbar', 'unknown', 'Field',
  'DemoState', 'guessMask', 'AnimatableProp',
]);

let mapError = null;
let mapDemoKind = null;
try {
  ({ demoCompFor: mapDemoKind } = require(MAP_PATH));
} catch (err) {
  mapError = err;
}

function requireMap() {
  assert.equal(
    mapError,
    null,
    `demoNav seam missing at ${MAP_PATH}: ${mapError && mapError.message} (RED: coder owns src/demoNav.js demoCompFor)`,
  );
  assert.equal(
    typeof mapDemoKind,
    'function',
    'demoNav must export demoCompFor(demoKind)',
  );
}

describe('T2 demo kind-map (RED pin)', () => {
  for (const [demo, generic] of EXACT_MAP) {
    it(`maps ${demo} to ${generic}`, () => {
      requireMap();
      assert.equal(mapDemoKind(demo), generic);
    });

    it(`mapped ${demo} resolves through the stub route`, () => {
      requireMap();
      const mapped = mapDemoKind(demo);
      assert.ok(
        STUB_KNOWN.has(mapped),
        `mapped comp '${mapped}' must resolve through the stub route`,
      );
    });
  }

  for (const kind of FAIL_CLOSED_KINDS) {
    it(`fails closed NAMED on '${kind}'`, () => {
      requireMap();
      assert.throws(() => mapDemoKind(kind), (err) => {
        assert.ok(err instanceof Error, 'must throw an Error');
        assert.ok(
          err.message.includes(`unknown demo kind '${kind}'`),
          `diagnostic must name the kind (got: ${err.message})`,
        );
        return true;
      });
    });
  }

  it('never emits a demo-specific or serializer kind', () => {
    requireMap();
    const inputs = EXACT_MAP.map(([demo]) => demo).concat(FAIL_CLOSED_KINDS);
    for (const input of inputs) {
      let out;
      try {
        out = mapDemoKind(input);
      } catch (err) {
        // Fail-closed throw is the accepted non-emission path.
        assert.ok(
          err.message.includes('unknown demo kind'),
          `throw must be named (got: ${err.message})`,
        );
        continue;
      }
      assert.ok(
        !FORBIDDEN_EMISSIONS.has(out),
        `input '${input}' emitted forbidden kind '${out}'`,
      );
      assert.ok(
        STUB_KNOWN.has(out),
        `input '${input}' emitted unresolvable kind '${out}'`,
      );
    }
  });
});
