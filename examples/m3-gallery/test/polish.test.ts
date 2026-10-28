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

const LITERAL_COLOUR = /#[0-9a-fA-F]{3,8}\b|\brgba?\s*\(|\bhsla?\s*\(/;

function assertNoLiteralColours(target) {
  const src = readDemoFile(target, 'src/App.tsx');
  const hits = src.split('\n').filter((line) => {
    const code = line.split('//')[0];
    return LITERAL_COLOUR.test(code);
  });
  assert.deepEqual(hits, [], `literal colour in ${target}/src/App.tsx: ${JSON.stringify(hits)}`);
}
