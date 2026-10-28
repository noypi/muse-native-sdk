'use strict';
// Batch 1 (T0/T1) polish contract — FR-SAMPLE-011 shell subset + FR-SAMPLE-007/009
// shell-only scaffolds. TC-SAMPLE-027 (AC-SAMPLE-027) is manual; this file
// covers its unit-verifiable half as static source assertions.
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. Reads
// demo sources as text; never builds, never launches, never touches network,
// secrets, application config, or live services. No shared mutable state.
//
// Binds ONLY to the plan interfaces:
//   T0: `muse-native build <target>` exit 0 per target (NOT unit-runnable here:
//       needs the muse-native toolchain; marked UNVERIFIED, owner tester);
//       `App(): JSX.Element` default export per target (verified statically).
//   T1: `App(): JSX.Element` — renders `TopAppBar` + navigator (rail/bar
//       gallery; TopAppBar+HUD flappy; Lens/Field/List tabs muse-demo);
//       zero literal colours in demo chrome.
// Never binds to an implementation.
//
// ASSUMPTIONS:
//   A1 (static shape stands in for render): asserting on App.tsx source text
//       (TopAppBar/navigator identifiers, default export) is the closest
//       hermetic proxy for "renders"; actual on-device rendering is manual
//       TC-SAMPLE-027, owner tester.
//   A2 (chrome scope): the "zero literal colours" check covers each demo's
//       App.tsx only (the T1 Files list); theme-token usage deeper in helper
//       modules is out of scope for this batch.
//   A3 (literal-colour pattern): hex (#rgb/#rrggbb/#rrggbbaa), rgb()/rgba(),
//       hsl()/hsla() literals count as literal colours; named theme role
//       tokens and `transparent`/`currentColor` do not.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join } = require('node:path');

const EXAMPLES = join(__dirname, '..', '..');

function targetDir(name) {
  return join(EXAMPLES, name);
}
function readDemoFile(target, rel) {
  return readFileSync(join(targetDir(target), rel), 'utf8');
}
function existsDemoFile(target, rel) {
  return existsSync(join(targetDir(target), rel));
}

// --- T0: minimal scaffold targets (FR-SAMPLE-007/009 shell-only subset) ---

describe('T0 flappy scaffold exists (FR-SAMPLE-007 shell-only)', () => {
  it('owns a per-project muse-native.yaml that names the flappy target', () => {
    assert.ok(existsDemoFile('flappy', 'muse-native.yaml'), 'examples/flappy/muse-native.yaml missing');
    const yaml = readDemoFile('flappy', 'muse-native.yaml');
    assert.match(yaml, /name:\s*flappy/, 'muse-native.yaml must name the flappy target');
  });

  it('owns a package.json for the flappy target', () => {
    assert.ok(existsDemoFile('flappy', 'package.json'), 'examples/flappy/package.json missing');
    const pkg = JSON.parse(readDemoFile('flappy', 'package.json'));
    assert.ok(typeof pkg.name === 'string' && pkg.name.length > 0, 'package.json must declare a name');
  });

  it('exposes App(): JSX.Element as a default export', () => {
    assert.ok(existsDemoFile('flappy', 'src/App.tsx'), 'examples/flappy/src/App.tsx missing');
    const src = readDemoFile('flappy', 'src/App.tsx');
    assert.match(src, /export\s+default\s+function\s+App\s*\(/, 'App.tsx must default-export function App()');
  });
});

describe('T0 muse-demo scaffold exists (FR-SAMPLE-009 shell-only)', () => {
  it('owns a per-project muse-native.yaml that names the muse-demo target', () => {
    assert.ok(existsDemoFile('muse-demo', 'muse-native.yaml'), 'examples/muse-demo/muse-native.yaml missing');
    const yaml = readDemoFile('muse-demo', 'muse-native.yaml');
    assert.match(yaml, /name:\s*muse-demo/, 'muse-native.yaml must name the muse-demo target');
  });

  it('owns a package.json for the muse-demo target', () => {
    assert.ok(existsDemoFile('muse-demo', 'package.json'), 'examples/muse-demo/package.json missing');
    const pkg = JSON.parse(readDemoFile('muse-demo', 'package.json'));
    assert.ok(typeof pkg.name === 'string' && pkg.name.length > 0, 'package.json must declare a name');
  });

  it('exposes App(): JSX.Element as a default export', () => {
    assert.ok(existsDemoFile('muse-demo', 'src/App.tsx'), 'examples/muse-demo/src/App.tsx missing');
    const src = readDemoFile('muse-demo', 'src/App.tsx');
    assert.match(src, /export\s+default\s+function\s+App\s*\(/, 'App.tsx must default-export function App()');
  });
});

// --- T1: themed M3 shell per demo (AC-SAMPLE-027) ---

describe('T1 gallery themed shell (AC-SAMPLE-027)', () => {
  it('App renders a TopAppBar', () => {
    const src = readDemoFile('m3-gallery', 'src/App.tsx');
    assert.match(src, /TopAppBar/, 'gallery App.tsx must render TopAppBar');
  });

  it('App renders a rail/bar navigator', () => {
    const src = readDemoFile('m3-gallery', 'src/App.tsx');
    assert.ok(
      /NavigationRail/.test(src) || /NavigationBar/.test(src),
      'gallery App.tsx must render a NavigationRail or NavigationBar navigator',
    );
  });

  it('demo chrome carries zero literal colours', () => {
    assertNoLiteralColours('m3-gallery');
  });
});

describe('T1 flappy themed shell (AC-SAMPLE-027)', () => {
  it('App renders a TopAppBar', () => {
    const src = readDemoFile('flappy', 'src/App.tsx');
    assert.match(src, /TopAppBar/, 'flappy App.tsx must render TopAppBar');
  });

  it('App renders a HUD alongside the TopAppBar', () => {
    const src = readDemoFile('flappy', 'src/App.tsx');
    assert.match(src, /HUD/, 'flappy App.tsx must render a HUD navigator element');
  });

  it('demo chrome carries zero literal colours', () => {
    assertNoLiteralColours('flappy');
  });
});

describe('T1 muse-demo themed shell (AC-SAMPLE-027)', () => {
  it('App renders Lens/Field/List tabs', () => {
    const src = readDemoFile('muse-demo', 'src/App.tsx');
    assert.match(src, /['"]lens['"]/, 'muse-demo App.tsx must define a lens tab');
    assert.match(src, /['"]field['"]/, 'muse-demo App.tsx must define a field tab');
    assert.match(src, /['"]list['"]/, 'muse-demo App.tsx must define a list tab');
  });

  it('App renders a TopAppBar (shared themed shell)', () => {
    const src = readDemoFile('muse-demo', 'src/App.tsx');
    assert.match(src, /TopAppBar/, 'muse-demo App.tsx must render TopAppBar');
  });

  it('demo chrome carries zero literal colours', () => {
    assertNoLiteralColours('muse-demo');
  });
});

// Batch 2 (T2/T3/T4) polish contract — FR-SAMPLE-011 theme/state/motion subset.
// TC-SAMPLE-028 (AC-SAMPLE-028) is external (device-lab/demo-themes);
// TC-SAMPLE-029 (AC-SAMPLE-029) and TC-SAMPLE-030 (AC-SAMPLE-030) are manual /
// grep checks; this file covers their unit-verifiable half as static source
// assertions.
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. Reads
// demo sources as text; never builds, never launches, never touches network,
// secrets, application config, or live services. No shared mutable state.
//
// Binds ONLY to the plan interfaces:
//   T2: `applyTheme(mode: 'light'|'dark', seedFallback: boolean): void` —
//       switches palette via M3 role tokens; no hard-coded surface.
//   T3: `DemoState='empty'|'loading'|'ready'|'error'`; `onRetry(): void`
//       re-fires the failed load.
//   T4: `transition: Record<AnimatableProp,MotionToken>`;
//       `spring(initial)` handle with `value` + `animateTo(target,{velocity})`;
//       `sharedId` on hero nodes where named — all per AC-TSX-029/030/031
//       (consume only). Never binds to an implementation.
//
// ASSUMPTIONS:
//   B1 (static shape stands in for runtime): asserting on App.tsx source text
//       (applyTheme/DemoState/onRetry/transition/spring/sharedId identifiers)
//       is the closest hermetic proxy; actual palette rendering, state UX, and
//       animated transitions are manual/external TC-SAMPLE-028/029/030, owner
//       tester/device-lab.
//   B2 (scope): checks cover each demo's `src/App.tsx` only (the T2/T3/T4
//       Files list); helper modules are out of scope this batch.
//   B3 (role-token pattern): `surface`/`onSurface`/`primary`/`secondary`/
//       `tertiary`/`background` (with `*Container`/`*Variant` variants) count
//       as M3 role tokens in code — `//` comment tails are stripped before
//       matching so English prose ("error surface") does not count; bare
//       `error` is excluded because it collides with error-handling
//       identifiers, and hex/rgb()/hsl() literals do not count.
//   B4 (motion-literal pattern): `cubic-bezier(...)`, `easeIn*`/`easeOut*`/
//       `ease-in*` identifiers, and `duration: <number>` literals count as
//       literal curve/duration; motion-token references do not.
//   B5 (sharedId conditional): `sharedId` is required on hero nodes where the
//       demo names one; the static proxy requires the identifier in App.tsx
//       and the coder keeps it on the hero node.

const LITERAL_COLOUR = /#[0-9a-fA-F]{3,8}\b|\brgba?\s*\(|\bhsla?\s*\(/;
const ROLE_TOKEN = /\b(surface|onSurface|primary|secondary|tertiary|background)(Container|Variant)?\b/;
const LITERAL_MOTION = /cubic-bezier|easeIn[A-Z]|easeOut[A-Z]|ease-in|ease-out|duration\s*:\s*\d/;

function roleTokenInCode(src) {
  return src.split('\n').some((line) => ROLE_TOKEN.test(line.split('//')[0]));
}

function assertNoLiteralMotion(target) {  const src = readDemoFile(target, 'src/App.tsx');
  const hits = src.split('\n').filter((line) => {
    const code = line.split('//')[0];
    return LITERAL_MOTION.test(code);
  });
  assert.deepEqual(hits, [], `literal curve/duration in ${target}/src/App.tsx: ${JSON.stringify(hits)}`);
}

// --- T2: light/dark/Dynamic Color seed-fallback palettes (AC-SAMPLE-028) ---

for (const target of ['m3-gallery', 'flappy', 'muse-demo']) {
  describe(`T2 ${target} theme switch (AC-SAMPLE-028)`, () => {
    it('exposes applyTheme(mode, seedFallback)', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /applyTheme/, `${target} App.tsx must expose applyTheme`);
    });

    it("applyTheme takes mode 'light'|'dark' plus seedFallback", () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /['"]light['"]/, `${target} App.tsx must name a 'light' mode`);
      assert.match(src, /['"]dark['"]/, `${target} App.tsx must name a 'dark' mode`);
      assert.match(src, /seedFallback/, `${target} App.tsx must take seedFallback`);
    });

    it('switches palette via M3 role tokens with no hard-coded surface', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.ok(roleTokenInCode(src), `${target} App.tsx must reference M3 role tokens`);
      assertNoLiteralColours(target);
    });
  });
}

// --- T3: designed empty/loading/ready/error states with retry (AC-SAMPLE-029) ---

for (const target of ['m3-gallery', 'flappy', 'muse-demo']) {
  describe(`T3 ${target} demo states (AC-SAMPLE-029)`, () => {
    it("defines DemoState 'empty'|'loading'|'ready'|'error'", () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /DemoState/, `${target} App.tsx must define DemoState`);
      for (const state of ["'empty'", "'loading'", "'ready'", "'error'"]) {
        assert.ok(src.includes(state), `${target} App.tsx must name DemoState ${state}`);
      }
    });

    it('exposes onRetry() that re-fires the failed load', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /onRetry/, `${target} App.tsx must expose onRetry`);
    });

    it('renders a retry action with no raw exception text', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.ok(
        /[Rr]etry/.test(src) || /onRetry/.test(src),
        `${target} App.tsx must render a retry action`,
      );
      assert.ok(
        !/stack\s*trace|ENOENT|TypeError|Unhandled/.test(src),
        `${target} App.tsx must not leak raw exception text`,
      );
    });
  });
}

// --- T4: M3 motion tokens only (AC-SAMPLE-030; AC-TSX-029/030/031 consume-only) ---

for (const target of ['m3-gallery', 'flappy', 'muse-demo']) {
  describe(`T4 ${target} motion tokens (AC-SAMPLE-030)`, () => {
    it('declares a transition token map (TC-SAMPLE-030 grep)', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /transition\s*:/, `${target} App.tsx must declare transition`);
    });

    it('consumes spring(initial) with value + animateTo(target, { velocity })', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /spring\s*\(/, `${target} App.tsx must consume spring(initial)`);
      assert.match(src, /animateTo/, `${target} App.tsx must consume animateTo`);
      assert.match(src, /velocity/, `${target} App.tsx must pass velocity`);
    });

    it('sets sharedId on hero nodes with no literal curve/duration', () => {
      const src = readDemoFile(target, 'src/App.tsx');
      assert.match(src, /sharedId/, `${target} App.tsx must set sharedId on hero nodes`);
      assertNoLiteralMotion(target);
    });
  });
}

function assertNoLiteralColours(target) {
  const src = readDemoFile(target, 'src/App.tsx');
  const hits = src.split('\n').filter((line) => {
    const code = line.split('//')[0];
    return LITERAL_COLOUR.test(code);
  });
  assert.deepEqual(hits, [], `literal colour in ${target}/src/App.tsx: ${JSON.stringify(hits)}`);
}
