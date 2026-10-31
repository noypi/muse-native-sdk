'use strict';
// T61 RECONCILED (BUG-SAMPLE-003) — state coverage lives ONLY as
// manifest-generated story variants via buildStories, never as hand-listed rows.
//
// Reconciliation: SPEC-SAMPLE-PROJECT v2.37 AC-SAMPLE-041 forbids hand-edited
// label rows, so the pre-cleanup assertions in this file (GalleryRow rows
// EXIST with literal state="hover"|"focus"|"pressed"|"disabled" tokens)
// contradicted the spec and the cleaned tree. Plan T2/A1 retains T61 state
// coverage as manifest-generated variants via `buildStories`, never hand-listed.
// Coder finding: Surface/Ripple declare NO boolean state props (only
// TextFieldEditCore declares `enabled`); hover/focus/pressed/disabled are
// exercised via driveState, not gallery story state — so this file must NOT
// invent gallery stories for unwired states.
//
// Binds ONLY to:
//   Files: `examples/m3-gallery/src/Showcase.tsx`,
//          `examples/m3-gallery/src/stories.js` (+ real manifest JSON)
//   Interface: none — static JSX tree asset plus pure
//     `buildStories(manifest: Manifest): Story[]` (existing signature unchanged);
//     test surface is the parsed source (zero GalleryRow rows) plus the
//     generated story set for Surface/Ripple/TextFieldEditCore as ACTUALLY
//     emitted against the real manifest (probed, asserted verbatim, no more).
//   Acceptance: zero `<GalleryRow` rows and zero stub in Showcase.tsx; the
//     generated set contains Surface/Ripple × {default} × light/dark and
//     TextFieldEditCore × {default, enabled} × light/dark — and nothing else
//     for these components (no hover/focus/pressed/disabled gallery stories).
// Spec refs: AC-SAMPLE-041 (no hand-edited label rows — spec wins over the
//   stale test); AC-M3-015/016 (manifest-generated gallery); FR-M3-009 /
//   AC-M3-020 (TC-M3-021) state coverage via generation.
// Hermetic: node:test + node:assert/strict + node:fs only, plus require() of
//   the dependency-free `src/stories.js`. Parses committed source text and the
//   real manifest JSON from disk; never renders, never touches a device,
//   network, secrets, or config.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const GALLERY_DIR = join(__dirname, '..');
const SHOWCASE_PATH = join(GALLERY_DIR, 'src', 'Showcase.tsx');
const STORIES_PATH = join(GALLERY_DIR, 'src', 'stories.js');
// Real single source of truth — the same file stories.js documents.
const MANIFEST_PATH = join(GALLERY_DIR, '..', '..', 'packages', 'native', 'muse.manifest.json');

const THEMES = ['light', 'dark'];
// Probed emission truth (2026-10-31, real manifest):
//   Surface: props {tone: token, elevation: number} → owns NO STATE_PROPS
//     boolean → states ['default']; no `values` → 1 default variant.
//   Ripple: props {color: token, bounded: boolean} → `bounded` not in
//     STATE_PROPS → states ['default']; no `values` → 1 default variant.
//   TextFieldEditCore: props {value, placeholder, enabled: boolean} → owns
//     `enabled` → states ['default', 'enabled']; no `values` → 1 variant.
// Hence: Surface 1×2×1 = 2 stories, Ripple 1×2×1 = 2,
// TextFieldEditCore 1×2×2 = 4. Re-probe (not hand-edit) if manifest props change.
const EXPECTED = {
  Surface: { states: ['default'], count: 2 },
  Ripple: { states: ['default'], count: 2 },
  TextFieldEditCore: { states: ['default', 'enabled'], count: 4 },
};
// Interaction states exercised via driveState, never gallery story state.
const NON_STORY_STATES = ['hover', 'focus', 'pressed', 'disabled'];

function readShowcase() {
  let raw = null;
  try {
    raw = readFileSync(SHOWCASE_PATH, 'utf8');
  } catch (err) {
    assert.fail(`Showcase.tsx missing at ${SHOWCASE_PATH}: ${err.message}`);
  }
  assert.ok(raw.length > 0, 'Showcase.tsx must be non-empty');
  return raw;
}

function loadStoriesModule() {
  let mod = null;
  try {
    mod = require(STORIES_PATH);
  } catch (err) {
    assert.fail(`stories.js missing/unloadable at ${STORIES_PATH}: ${err.message}`);
  }
  assert.equal(typeof mod.buildStories, 'function', 'stories.js must export buildStories(manifest)');
  return mod;
}

function loadManifest() {
  let raw = null;
  try {
    raw = readFileSync(MANIFEST_PATH, 'utf8');
  } catch (err) {
    assert.fail(`manifest missing at ${MANIFEST_PATH}: ${err.message}`);
  }
  return JSON.parse(raw);
}

describe('T61 reconciled — Showcase.tsx carries zero hand-listed state rows (AC-SAMPLE-041)', () => {
  it('contains zero `<GalleryRow` row references', () => {
    const src = readShowcase();
    const refs = src.match(/<GalleryRow\b/g) || [];
    assert.equal(
      refs.length,
      0,
      `Showcase.tsx must contain zero <GalleryRow references, found ${refs.length} (AC-SAMPLE-041: no hand-edited label rows)`,
    );
  });

  it('contains zero stub `function GalleryRow` definitions', () => {
    const src = readShowcase();
    const stubs = src.match(/function\s+GalleryRow\b/g) || [];
    assert.equal(
      stubs.length,
      0,
      `Showcase.tsx must contain zero stub function GalleryRow, found ${stubs.length}`,
    );
  });

  it('contains no hand-listed interaction-state literals (driveState owns these, never gallery rows)', () => {
    const src = readShowcase();
    for (const state of NON_STORY_STATES) {
      const re = new RegExp(`state\\s*=\\s*("|')${state}\\1`);
      assert.ok(
        !re.test(src),
        `Showcase.tsx must not hand-list state="${state}" (exercised via driveState, not gallery story state)`,
      );
    }
  });
});

describe('T61 reconciled — buildStories emits the manifest-generated state variants (AC-M3-015/016)', () => {
  it('stories.js exports buildStories with STATE_PROPS/THEMES and no interaction-state vocabulary', () => {
    const mod = loadStoriesModule();
    assert.ok(Array.isArray(mod.STATE_PROPS), 'stories.js must export STATE_PROPS');
    assert.deepEqual(mod.THEMES, THEMES, 'stories.js THEMES must be [light, dark]');
    for (const state of NON_STORY_STATES) {
      assert.ok(
        !mod.STATE_PROPS.includes(state),
        `STATE_PROPS must not contain "${state}" (driveState owns it; do NOT invent gallery stories for unwired states)`,
      );
    }
  });

  it('buildStories is pure over the real manifest (never mutates its input)', () => {
    const { buildStories } = loadStoriesModule();
    const manifest = loadManifest();
    const before = JSON.stringify(manifest);
    const stories = buildStories(manifest);
    assert.ok(Array.isArray(stories) && stories.length > 0, 'buildStories(manifest) must return a non-empty array');
    assert.equal(JSON.stringify(manifest), before, 'buildStories must not mutate its input');
  });

  for (const name of Object.keys(EXPECTED)) {
    it(`${name} emits exactly states [${EXPECTED[name].states.join(', ')}] × light/dark (${EXPECTED[name].count} stories, probed)`, () => {
      const { buildStories } = loadStoriesModule();
      const stories = buildStories(loadManifest()).filter((s) => s.component === name);
      assert.equal(
        stories.length,
        EXPECTED[name].count,
        `${name}: expected ${EXPECTED[name].count} generated stories, got ${stories.length}`,
      );
      const states = [...new Set(stories.map((s) => s.state))].sort();
      assert.deepEqual(states, [...EXPECTED[name].states].sort(), `${name}: emitted states must be exactly [${EXPECTED[name].states.join(', ')}]`);
      for (const state of EXPECTED[name].states) {
        for (const theme of THEMES) {
          assert.ok(
            stories.some((s) => s.state === state && s.theme === theme),
            `${name}: no generated story for state="${state}" theme="${theme}"`,
          );
        }
      }
    });

    it(`${name} emits no hover/focus/pressed/disabled gallery stories (unwired states)`, () => {
      const { buildStories } = loadStoriesModule();
      const stories = buildStories(loadManifest()).filter((s) => s.component === name);
      for (const story of stories) {
        assert.ok(
          !NON_STORY_STATES.includes(story.state),
          `${name}: story state "${story.state}" is a driveState concern, not a gallery story state`,
        );
      }
    });

    it(`${name} render() descriptors name the component and propagate theme`, () => {
      const { buildStories } = loadStoriesModule();
      const stories = buildStories(loadManifest()).filter((s) => s.component === name);
      assert.ok(stories.length > 0, `${name}: no stories to render`);
      for (const story of stories) {
        const descriptor = story.render();
        assert.equal(descriptor.type, name, 'descriptor type must name the component');
        assert.equal(descriptor.theme, story.theme, 'descriptor must propagate the story theme');
        assert.equal(descriptor.state, story.state, 'descriptor must echo the story state');
      }
    });
  }
});

describe('T61 — Showcase stays fully static JSX (no hooks/expressions)', () => {
  it('no hooks, imports of runtime state, or device calls', () => {
    const src = readShowcase();
    for (const banned of ['useState', 'useEffect', 'useRef', 'fetch(', 'require(', 'adb']) {
      assert.ok(!src.includes(banned), `static Showcase.tsx must not contain \`${banned}\``);
    }
  });

  it('token-only: no literal colour or dimension (AC-M3-019)', () => {
    const src = readShowcase();
    assert.ok(!/#[0-9a-fA-F]{3,8}\b/.test(src), 'no hex colour literal allowed');
    assert.ok(!/rgb\(/i.test(src), 'no rgb() literal allowed');
    assert.ok(!/\d+(px|dp|sp|pt)\b/.test(src), 'no px/dp/sp/pt dimension literal allowed');
  });
});
