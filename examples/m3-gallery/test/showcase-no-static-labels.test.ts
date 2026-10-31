'use strict';
// T1 — Showcase carries no static Text-label duplicates; static path survives
// only behind an explicit reporting fallback flag that fails the proof gate.
// Binds ONLY to the plan T1 item:
//   Files: `examples/m3-gallery/test/showcase-no-static-labels.test.ts` (new)
//   Interface: `node:test` suite `showcase-no-static-labels` — asserts (a)
//     `Showcase.tsx` contains zero `<GalleryRow` references and zero stub
//     `function GalleryRow`, (b) `App.tsx` story block contains no `<Text>`
//     label preceding `StoryComponent` render, (c) default mode renders
//     generated tags (`getGalleryMode(): 'generated' | 'static-fallback'`
//     returns `'generated'` without flag), (d) flag mode reports fallback and
//     fails the proof gate (exit non-zero / `fail-on-static-fallback` trips).
//     Hermetic: `node:test` + `node:assert/strict` + `node:fs` source/
//     tree-shape assertions only, no device/network/secrets.
// Spec refs: AC-SAMPLE-041, TC-SAMPLE-041; AC-RUNTIME-032 (flag/fail-gate clause).
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. Parses
// committed source text and drives the pure `getGalleryMode()`/
// `reportStaticFallback()` module in-process; never renders, never touches a
// device, network, secrets, or application config.
// ASSUMPTIONS (contract genuinely silent — plan A2 leaves flag name/shape to
// the coder; the single contract is explicit opt-in + reports fallback +
// fails proof gate):
//   A1 (flag name): the explicit opt-in flag is `MUSE_GALLERY_STATIC_FALLBACK=1`
//       (plan suggestion). The test also accepts the same token as a launch
//       flag / CLI argv token in source. If the coder renames it, this file's
//       FLAG_NAME constant is reconciled — expected behavior is unchanged.
//   A2 (module home): `getGalleryMode` / `reportStaticFallback` live in a
//       plain-JS gallery-mode module resolvable from the candidate paths
//       below (preferred `src/galleryMode.js`). If the coder homes them
//       elsewhere under `src/`, CANDIDATE_MODULES is reconciled.
//   A3 (report channel): `reportStaticFallback()` reports explicitly by
//       returning a non-empty string AND/OR writing to console
//       (warn/error/log). The test accepts either channel.
//   A4 (proof gate shape): the fail-closed gate is an exported
//       `assertNoStaticFallback()` (aliases accepted, see GATE_NAMES) that
//       throws / returns non-zero / exits non-zero in fallback mode, or a
//       committed `fail-on-static-fallback` wiring in CLI/proof sources. The
//       test accepts any of these shapes.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync, existsSync } = require('node:fs');
const { join } = require('node:path');

const GALLERY_DIR = join(__dirname, '..');
const SHOWCASE_PATH = join(GALLERY_DIR, 'src', 'Showcase.tsx');
const APP_PATH = join(GALLERY_DIR, 'src', 'App.tsx');

// ASSUMPTION A2: candidate homes for the gallery-mode module (plain JS only,
// so node --test can require it without a build step).
const CANDIDATE_MODULES = [
  join(GALLERY_DIR, 'src', 'galleryMode.js'),
  join(GALLERY_DIR, 'src', 'gallery-mode.js'),
  join(GALLERY_DIR, 'src', 'stories.js'),
];
// ASSUMPTION A1: explicit opt-in flag token.
const FLAG_NAME = 'MUSE_GALLERY_STATIC_FALLBACK';
// ASSUMPTION A4: accepted fail-closed gate export names.
const GATE_NAMES = [
  'assertNoStaticFallback',
  'checkStaticFallback',
  'failOnStaticFallback',
  'assertNoStaticFallbackGate',
];

function readSource(path) {
  let raw = null;
  try {
    raw = readFileSync(path, 'utf8');
  } catch (err) {
    assert.fail(`source missing at ${path}: ${err.message}`);
  }
  assert.ok(raw.length > 0, `${path} must be non-empty`);
  return raw;
}

// Resolve the gallery-mode module: first candidate that exports
// `getGalleryMode` as a function. Returns { mod, path } or fails red.
function resolveGalleryMode() {
  const tried = [];
  for (const path of CANDIDATE_MODULES) {
    if (!existsSync(path)) {
      tried.push(`${path} (absent)`);
      continue;
    }
    let mod = null;
    try {
      delete require.cache[require.resolve(path)];
      mod = require(path);
    } catch (err) {
      tried.push(`${path} (unrequirable: ${err.message})`);
      continue;
    }
    if (mod && typeof mod.getGalleryMode === 'function') return { mod, path };
    tried.push(`${path} (no getGalleryMode export)`);
  }
  assert.fail(
    `no gallery-mode module exports getGalleryMode(): 'generated' | 'static-fallback'. Tried: ${tried.join('; ')}`,
  );
}

function withCleanFlag(fn) {
  const savedNodeEnv = process.env[FLAG_NAME];
  const savedArgv = process.argv.slice();
  delete process.env[FLAG_NAME];
  process.argv = process.argv.filter((a) => !String(a).includes(FLAG_NAME));
  try {
    return fn();
  } finally {
    if (savedNodeEnv === undefined) delete process.env[FLAG_NAME];
    else process.env[FLAG_NAME] = savedNodeEnv;
    process.argv = savedArgv;
  }
}

function withFlagSet(fn) {
  const saved = process.env[FLAG_NAME];
  process.env[FLAG_NAME] = '1';
  try {
    return fn();
  } finally {
    if (saved === undefined) delete process.env[FLAG_NAME];
    else process.env[FLAG_NAME] = saved;
  }
}

describe('showcase-no-static-labels (a) Showcase.tsx has zero GalleryRow refs', () => {
  it('contains zero `<GalleryRow` references', () => {
    const src = readSource(SHOWCASE_PATH);
    const refs = src.match(/<GalleryRow\b/g) || [];
    assert.equal(
      refs.length,
      0,
      `Showcase.tsx must contain zero <GalleryRow references, found ${refs.length} (TC-SAMPLE-041 clause 2)`,
    );
  });

  it('contains zero stub `function GalleryRow` definitions', () => {
    const src = readSource(SHOWCASE_PATH);
    const stubs = src.match(/function\s+GalleryRow\b/g) || [];
    assert.equal(
      stubs.length,
      0,
      `Showcase.tsx must contain zero stub function GalleryRow, found ${stubs.length}`,
    );
  });

  it('boundary: no GalleryRow token in any casing/spelling variant', () => {
    const src = readSource(SHOWCASE_PATH);
    assert.ok(
      !/GalleryRow/.test(src),
      'Showcase.tsx must contain no GalleryRow token at all (usage or definition)',
    );
  });
});

describe('showcase-no-static-labels (b) App.tsx story block has no Text label', () => {
  // Slice the per-story render block: from stories.map to the end of the
  // file (the story loop is the last render section in App.tsx).
  function storyBlock() {
    const src = readSource(APP_PATH);
    const idx = src.indexOf('stories.map');
    assert.ok(idx >= 0, 'App.tsx must render its stories via stories.map');
    return src.slice(idx);
  }

  it('no `<Text>` label preceding `StoryComponent` render in the story block', () => {
    const block = storyBlock();
    // Anchor on the render usage (`<StoryComponent ... />` or
    // `content={<StoryComponent ... />}`), not the `const StoryComponent =`
    // binding above it.
    const usage = block.search(/content=\{<StoryComponent|<StoryComponent\b/);
    assert.ok(usage >= 0, 'story block must render StoryComponent');
    const before = block.slice(block.indexOf('return ('), usage);
    assert.ok(
      !/<Text\b/.test(before),
      'story block must not render a <Text> label before StoryComponent (AC-SAMPLE-041: no static Text-label duplicate rows)',
    );
  });

  it('no story-descriptor interpolation (`story.component ...`) inside a `<Text>`', () => {
    const block = storyBlock();
    const labelRe = /<Text[^>]*>\s*\{?\s*[`'"]?\$\{story\.component\}/;
    assert.ok(
      !labelRe.test(block),
      'App.tsx:228 pattern `{`${story.component} ...`}` inside <Text> must be gone from the story surface',
    );
  });

  it('boundary: empty story list still satisfies the gate (no label rows to render)', () => {
    const block = storyBlock();
    const textTags = block.match(/<Text\b/g) || [];
    assert.equal(
      textTags.length,
      0,
      `story block must carry zero <Text> tags at all, found ${textTags.length}`,
    );
  });
});

describe('showcase-no-static-labels (c) default mode is generated', () => {
  it("getGalleryMode() returns 'generated' without the flag", () => {
    withCleanFlag(() => {
      const { mod, path } = resolveGalleryMode();
      assert.equal(
        mod.getGalleryMode(),
        'generated',
        `default gallery mode from ${path} must be 'generated' (manifest-generated real tags)`,
      );
    });
  });

  it('default mode is wired to generated tags in source (fail-closed default)', () => {
    const app = readSource(APP_PATH);
    const showcase = readSource(SHOWCASE_PATH);
    const modeSources = CANDIDATE_MODULES.filter((p) => existsSync(p)).map((p) =>
      readSource(p),
    );
    const all = [app, showcase, ...modeSources].join('\n');
    assert.ok(
      /generated/.test(all),
      'gallery sources must name the generated default path',
    );
    assert.ok(
      all.includes(FLAG_NAME),
      `explicit opt-in flag ${FLAG_NAME} must exist in gallery sources (ASSUMPTION A1)`,
    );
  });
});

describe('showcase-no-static-labels (d) flag mode reports fallback and fails the proof gate', () => {
  let captured = [];
  let origWarn;
  let origError;
  let origLog;

  beforeEach(() => {
    captured = [];
    origWarn = console.warn;
    origError = console.error;
    origLog = console.log;
    console.warn = (...a) => captured.push(a.join(' '));
    console.error = (...a) => captured.push(a.join(' '));
    console.log = (...a) => captured.push(a.join(' '));
  });

  afterEach(() => {
    console.warn = origWarn;
    console.error = origError;
    console.log = origLog;
  });

  it("getGalleryMode() returns 'static-fallback' with the flag set", () => {
    withFlagSet(() => {
      const { mod, path } = resolveGalleryMode();
      assert.equal(
        mod.getGalleryMode(),
        'static-fallback',
        `flagged gallery mode from ${path} must be 'static-fallback'`,
      );
    });
  });

  it('flag mode explicitly reports the fallback (ASSUMPTION A3: return value and/or console)', () => {
    withFlagSet(() => {
      const { mod } = resolveGalleryMode();
      assert.equal(
        typeof mod.reportStaticFallback,
        'function',
        'gallery-mode module must export reportStaticFallback(): void',
      );
      captured = [];
      const ret = mod.reportStaticFallback();
      const reported =
        (typeof ret === 'string' && ret.length > 0) ||
        captured.some((line) => /fallback/i.test(line));
      assert.ok(
        reported,
        'reportStaticFallback() must explicitly report fallback via a non-empty return and/or console output mentioning fallback',
      );
    });
  });

  it('proof gate fails in fallback mode (fail-closed; ASSUMPTION A4)', () => {
    withFlagSet(() => {
      const { mod, path } = resolveGalleryMode();
      const gate = GATE_NAMES.find((n) => typeof mod[n] === 'function');
      if (gate) {
        let threw = false;
        let code = 0;
        try {
          const ret = mod[gate]();
          if (typeof ret === 'number') code = ret;
        } catch (err) {
          threw = true;
        }
        assert.ok(
          threw || code !== 0,
          `${gate}() from ${path} must throw or return non-zero in fallback mode (TC-RUNTIME-032 fail-closed)`,
        );
        return;
      }
      // No gate export: accept committed fail-on-static-fallback wiring.
      const cliProof = join(GALLERY_DIR, '..', '..', 'packages', 'cli', 'lib', 'gallery-proof.js');
      const galleryProof = join(GALLERY_DIR, 'src', 'gallery-proof.js');
      const haystacks = [APP_PATH, SHOWCASE_PATH, ...CANDIDATE_MODULES.filter((p) => existsSync(p))];
      if (existsSync(cliProof)) haystacks.push(cliProof);
      if (existsSync(galleryProof)) haystacks.push(galleryProof);
      const all = haystacks.map((p) => readSource(p)).join('\n');
      assert.ok(
        /fail-on-static-fallback/.test(all),
        'fallback mode must trip a fail-on-static-fallback proof gate (exported gate or committed CLI wiring)',
      );
    });
  });

  it('error path: unsetting the flag after fallback restores generated mode', () => {
    withFlagSet(() => {
      resolveGalleryMode();
    });
    withCleanFlag(() => {
      const { mod } = resolveGalleryMode();
      assert.equal(
        mod.getGalleryMode(),
        'generated',
        'clearing the flag must restore generated mode (no sticky fallback)',
      );
    });
  });
});
