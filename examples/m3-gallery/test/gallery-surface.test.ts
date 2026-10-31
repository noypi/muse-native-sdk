'use strict';
// FR-SAMPLE-014 Batch 2 (T3 contract).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. Synthetic
// in-memory manifests through the real pure `buildStories(manifest)`; no
// network, no secrets, no application config, no device, no live service, no
// shared mutable state. `process.env` is never read: flag decisions take an
// explicit `{ argv, env }` input with caller-supplied dummy values.
//
// Binds ONLY to the plan T3 interface (reproduced verbatim in unit-tests/T3.md):
//   `renderGallerySection(theme: 'light' | 'dark'): { tags: string[],
//   staticFallback: false }` under the default path;
//   `renderStaticFallback(reason: string): { staticFallback: true,
//   reported: true, proofGate: 'fail' }` reachable only via explicit flag
//   `--static-fallback` (CLI) / `MUSE_STATIC_FALLBACK=1` (env).
//   Default path never returns `staticFallback: true`.
//
// ASSUMPTIONS (plan Interface genuinely silent on module/file spelling —
// coder reconciles layout per GR-08 T1.9; behavior pinned, spelling
// reconcilable):
//   A1 (module path): the gallery surface entry lives at
//       `src/gallerySurface.js` (CommonJS) exporting `renderGallerySection`
//       and `renderStaticFallback`. The coder reconciles the exact file
//       layout; the behavior below is what must hold.
//   A2 (stories injection): tests drive sections through
//       `renderGallerySection(theme, stories)` where `stories` is a
//       caller-supplied `buildStories(manifest)` list. Production defaults it
//       to `buildStories(manifest)`; expectations do not change either way.
//   A3 (flag helper): the explicit-flag decision is exposed as
//       `isStaticFallbackEnabled({ argv, env })` returning a boolean. If the
//       coder nests it differently, call sites reconcile; the fail-closed
//       behavior below is what must hold.
//   A4 (proof-gate helper): the fail-closed gate decision is exposed as
//       `evaluateProofGate({ staticFallback })` returning
//       `{ pass: boolean }`, and the CLI carries
//       `gallery-tree --assert-evaluated --fail-on-static-fallback`.
//   A5 (invalid theme): the plan pins `theme: 'light' | 'dark'` but no error
//       shape for other input; tests assert a throw (any Error) — the
//       defensible fail-closed reading.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join, resolve } = require('node:path');

const SURFACE_PATH = '../src/gallerySurface.js';
const STORIES_PATH = '../src/stories.js';
const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const SHOWCASE_PATH = join(__dirname, '..', 'src', 'Showcase.tsx');
const CLI_SOURCES = [
  join(REPO_ROOT, 'packages', 'cli', 'bin', 'muse-native.js'),
  join(REPO_ROOT, 'packages', 'cli', 'lib', 'cli.js'),
  join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-tree.js'),
];

let surfaceError = null;
let surface = null;
try {
  surface = require(SURFACE_PATH);
} catch (err) {
  surfaceError = err;
}

let storiesError = null;
let buildStories = null;
try {
  ({ buildStories } = require(STORIES_PATH));
} catch (err) {
  storiesError = err;
}

function requireSurface() {
  assert.equal(
    surfaceError,
    null,
    `gallery surface module missing at ${SURFACE_PATH}: ${surfaceError && surfaceError.message}`,
  );
  for (const fn of ['renderGallerySection', 'renderStaticFallback']) {
    assert.equal(typeof surface[fn], 'function', `gallerySurface must export ${fn}()`);
  }
}

function requireFlagHelper() {
  requireSurface();
  assert.equal(
    typeof surface.isStaticFallbackEnabled,
    'function',
    'gallerySurface must export isStaticFallbackEnabled({ argv, env }) (A3)',
  );
}

function requireGateHelper() {
  requireSurface();
  assert.equal(
    typeof surface.evaluateProofGate,
    'function',
    'gallerySurface must export evaluateProofGate({ staticFallback }) (A4)',
  );
}

function syntheticManifest() {
  return {
    name: 'test-fixture-not-real',
    components: [
      { name: 'Button', tier: 1, props: { variant: { type: 'string', values: ['filled', 'outlined'] } } },
      { name: 'Card', tier: 2, props: { tone: { type: 'token' } } },
      { name: 'Dialog', tier: 2, props: { open: { type: 'boolean' } } },
    ],
  };
}

function demoStories() {
  assert.equal(storiesError, null, `stories module failed to load: ${storiesError && storiesError.message}`);
  assert.equal(typeof buildStories, 'function', 'stories module must export buildStories(manifest)');
  const stories = buildStories(syntheticManifest());
  assert.ok(stories.length > 0, 'synthetic manifest must yield stories');
  return stories;
}

function readText(path) {
  return readFileSync(path, 'utf8');
}

describe('T3 — default path renders manifest-generated real-tag themed sections', () => {
  it('exports renderGallerySection/renderStaticFallback', () => {
    requireSurface();
  });

  it('light section renders real component tags with staticFallback:false', () => {
    requireSurface();
    const out = surface.renderGallerySection('light', demoStories());
    assert.equal(out.staticFallback, false, 'default path must never return staticFallback:true');
    assert.ok(Array.isArray(out.tags) && out.tags.length > 0, 'light section must render tags');
    for (const tag of ['Button', 'Card', 'Dialog']) {
      assert.ok(out.tags.includes(tag), `light tags must include manifest component ${tag} (got ${JSON.stringify(out.tags)})`);
    }
  });

  it('dark section renders real component tags with staticFallback:false', () => {
    requireSurface();
    const out = surface.renderGallerySection('dark', demoStories());
    assert.equal(out.staticFallback, false, 'default path must never return staticFallback:true');
    assert.ok(Array.isArray(out.tags) && out.tags.length > 0, 'dark section must render tags');
    for (const tag of ['Button', 'Card', 'Dialog']) {
      assert.ok(out.tags.includes(tag), `dark tags must include manifest component ${tag} (got ${JSON.stringify(out.tags)})`);
    }
  });

  it('tags are manifest-derived, never hand-edited Text-label rows', () => {
    requireSurface();
    const stories = demoStories();
    const expected = [...new Set(stories.map((s) => s.component))].sort();
    for (const theme of ['light', 'dark']) {
      const out = surface.renderGallerySection(theme, stories);
      assert.deepEqual([...out.tags].sort(), expected, `${theme} tags must equal the manifest component set`);
      assert.ok(!out.tags.some((t) => /M3 Gallery (light|dark)/i.test(t)), 'tags must not contain static Text-label rows');
      assert.ok(!out.tags.includes('Text'), 'duplicate Text-label rows must be gone from the default tree');
    }
  });

  it('overlay members flow through the components table (Dialog kept, OverlayHost nesting available)', () => {
    requireSurface();
    const out = surface.renderGallerySection('light', demoStories());
    assert.ok(out.tags.includes('Dialog'), 'overlay member Dialog must survive in the default tree (RULE-M3-006)');
    const stories = demoStories();
    const dialogStory = stories.find((s) => s.component === 'Dialog');
    assert.ok(dialogStory, 'synthetic manifest must yield a Dialog story');
    const descriptor = dialogStory.render();
    assert.equal(descriptor.overlay, 'OverlayHost', 'overlay descriptors must nest via OverlayHost per RULE-M3-006');
  });

  it('invalid theme is rejected (fail-closed), never a blank/static surface', () => {
    requireSurface();
    for (const bad of ['sepia', '', null, undefined, { theme: 'light' }, '__proto__', 'light '.repeat(100)]) {
      assert.throws(
        () => surface.renderGallerySection(bad, demoStories()),
        Error,
        `theme ${JSON.stringify(String(bad)).slice(0, 40)} must throw (A5)`,
      );
    }
  });

  it('Showcase.tsx:12-108 static Text-label rows are gone or quarantined behind the explicit flag', () => {
    assert.ok(existsSync(SHOWCASE_PATH), 'src/Showcase.tsx must still exist (quarantined, not lost)');
    const src = readText(SHOWCASE_PATH);
    const hasStaticRows = /M3 Gallery light/.test(src) && /M3 Gallery dark/.test(src);
    const hasFlagGuard = /MUSE_STATIC_FALLBACK|static-fallback/.test(src);
    assert.ok(
      !hasStaticRows || hasFlagGuard,
      'static Text-label rows present without explicit-flag quarantine: Showcase.tsx:12-108 duplicates must be deleted or guarded by --static-fallback / MUSE_STATIC_FALLBACK=1',
    );
  });
});

describe('T3 — static fallback is reachable only via the explicit flag and reports + fails the proof gate', () => {
  it('explicit flag is off by default (empty argv, empty env)', () => {
    requireFlagHelper();
    assert.equal(surface.isStaticFallbackEnabled({ argv: [], env: {} }), false);
  });

  it('--static-fallback enables the fallback path', () => {
    requireFlagHelper();
    assert.equal(surface.isStaticFallbackEnabled({ argv: ['--static-fallback'], env: {} }), true);
  });

  it('MUSE_STATIC_FALLBACK=1 enables the fallback path', () => {
    requireFlagHelper();
    assert.equal(surface.isStaticFallbackEnabled({ argv: [], env: { MUSE_STATIC_FALLBACK: '1' } }), true);
  });

  it('near-miss env values stay fail-closed (no fallback)', () => {
    requireFlagHelper();
    for (const value of ['0', '', 'yes', 'true', '2']) {
      assert.equal(
        surface.isStaticFallbackEnabled({ argv: [], env: { MUSE_STATIC_FALLBACK: value } }),
        false,
        `MUSE_STATIC_FALLBACK=${JSON.stringify(value)} must not enable the fallback (only exactly '1')`,
      );
    }
  });

  it('unrelated flags never enable the fallback', () => {
    requireFlagHelper();
    assert.equal(surface.isStaticFallbackEnabled({ argv: ['--assert-evaluated'], env: {} }), false);
    assert.equal(surface.isStaticFallbackEnabled({ argv: ['--fail-on-static-fallback'], env: {} }), false);
    assert.equal(surface.isStaticFallbackEnabled({ argv: ['gallery-tree'], env: {} }), false);
  });

  it('renderStaticFallback reports the fallback and fails the proof gate', () => {
    requireSurface();
    const out = surface.renderStaticFallback('test-reason-not-real');
    assert.equal(out.staticFallback, true, 'fallback must mark staticFallback:true');
    assert.equal(out.reported, true, 'fallback must report itself');
    assert.equal(out.proofGate, 'fail', 'fallback must fail the proof gate');
  });

  it('fallback report survives an empty or hostile reason without flipping the gate', () => {
    requireSurface();
    for (const reason of ['', 'pass', '__proto__', 'x'.repeat(10000)]) {
      const out = surface.renderStaticFallback(reason);
      assert.equal(out.reported, true, 'fallback must report even for an empty/hostile reason');
      assert.equal(out.proofGate, 'fail', 'no reason string may flip the proof gate to pass');
    }
  });

  it('proof gate passes on the default tree and fails on the static fallback', () => {
    requireGateHelper();
    assert.equal(surface.evaluateProofGate({ staticFallback: false }).pass, true, 'default tree must pass the gate');
    assert.equal(surface.evaluateProofGate({ staticFallback: true }).pass, false, 'static fallback must fail the gate');
  });

  it('CLI exposes gallery-tree --assert-evaluated --fail-on-static-fallback (AC-RUNTIME-032)', () => {
    const haystack = CLI_SOURCES.filter(existsSync).map(readText).join('\n');
    assert.ok(/gallery-tree/.test(haystack), 'CLI must expose the gallery-tree command');
    assert.ok(haystack.includes('--assert-evaluated'), 'CLI gallery-tree must support --assert-evaluated');
    assert.ok(haystack.includes('--fail-on-static-fallback'), 'CLI gallery-tree must support --fail-on-static-fallback');
  });

  it('gallery-tree --fail-on-static-fallback --static-fallback throws (single-flag gate, no --assert-evaluated needed)', () => {
    const treePath = join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-tree.js');
    assert.ok(existsSync(treePath), 'packages/cli/lib/gallery-tree.js must exist');
    const tree = require(treePath);
    assert.equal(typeof tree.runGalleryTree, 'function', 'gallery-tree must export runGalleryTree(argv, env)');
    assert.throws(
      () => tree.runGalleryTree(['--fail-on-static-fallback', '--static-fallback'], {}),
      /static fallback/i,
      'single-flag --fail-on-static-fallback with a static fallback must throw (exit non-zero), not return proofGate:fail with exit 0',
    );
  });
});
