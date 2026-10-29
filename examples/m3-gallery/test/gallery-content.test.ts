'use strict';
// Batch 1 (T1/T2) gallery fidelity contract — FR-M3-007.
// TC-M3-015 regression companion (AC-M3-015) + AC-M3-016 story-row rendering.
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. Reads
// Showcase.tsx / App.tsx sources as text (static-literal TSX tree proxy);
// never builds, never launches, never touches network, secrets, application
// config, or live services. No shared mutable state.
//
// Binds ONLY to the plan interfaces:
//   T1: none — static TSX tree only (no exported function signature change;
//       default-exported Showcase() retained).
//   T2: none — same default-exported App(); story rows render story.render()
//       descriptor type as tag.
// Never binds to an implementation.
//
// ASSUMPTIONS:
//   A1 (static shape stands in for render): asserting on TSX source text
//       (themed View sections, real component tags, Text stubs) is the
//       closest hermetic proxy for "themed sections contain real tags";
//       actual on-device rendering is T3, owner tester/manual.
//   A2 (real-tag vocabulary): tags other than View/Text closed from the M3
//       component set (TopAppBar, Button, Card, Chip, Checkbox, Radio,
//       Switch, Slider, LinearProgress, CircularProgress, Badge, ListItem,
//       Avatar, Icon, Image, Divider, TextFieldEditCore, TextInput, Dialog,
//       BottomSheet, Menu, SnackbarHost, Tooltip, Carousel, Surface, Scroll,
//       Stack, Ripple, OverlayHost, NavigationBar, NavigationRail,
//       NavigationDrawer, BottomAppBar, SearchBar, SegmentedButton,
//       ButtonGroup, SplitButton, IconButton, Fab, ExtendedFab) count as
//       real tags. View/Text alone count as stubs.
//   A3 (descriptor-type-as-tag proxy): a story row rendering the descriptor
//       type as a tag is observed statically as App.tsx referencing
//       `rendered.type` in a JSX tag position; the exact JSX spelling the
//       coder chooses may vary, so the contract pins the observable (no
//       JSON.stringify of the descriptor + rendered.type used as a tag),
//       not the spelling.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const SHOWCASE_PATH =
  process.env.SHOWCASE_PATH ?? join(__dirname, '..', 'src', 'Showcase.tsx');
const APP_PATH = process.env.APP_PATH ?? join(__dirname, '..', 'src', 'App.tsx');

function readSource(path) {
  return readFileSync(path, 'utf8');
}

// Split Showcase.tsx into the light-themed, dark-themed, and any unthemed
// trailing View sections by theme attribute.
function themedSection(src, theme) {
  const open = `<View theme="${theme}">`;
  const start = src.indexOf(open);
  assert.ok(start !== -1, `Showcase.tsx must contain ${open}`);
  const afterOpen = start + open.length;
  // The themed section ends at the first closing </View> that is followed
  // by either the next section header or the end of the component. Use a
  // simple depth scan over <View...>/<View> vs </View> tokens.
  const tagRe = /<\/?View[\s>]/g;
  tagRe.lastIndex = afterOpen;
  let depth = 1;
  let m = null;
  while ((m = tagRe.exec(src)) !== null) {
    if (m[0] === '</V' || m[0].startsWith('</View')) depth -= 1;
    else depth += 1;
    if (depth === 0) return src.slice(afterOpen, m.index);
  }
  assert.fail(`unterminated themed View "${theme}" in Showcase.tsx`);
  return '';
}

function realTagsPresent(section) {
  const tags = [...section.matchAll(/<([A-Z][A-Za-z]*)\b/g)].map((m) => m[1]);
  return tags.filter((t) => t !== 'View' && t !== 'Text');
}

function stubOnlyRows(section) {
  return section.split('\n').filter((line) => {
    const t = line.trim();
    return t.startsWith('<Text') && !t.includes('{');
  });
}

// --- T1 (AC-M3-015 / FR-M3-007): themed sections carry the real tags ---

describe('T1 light themed section contains real tags (AC-M3-015)', () => {
  it('light section renders real M3 tags, not Text stubs only', () => {
    const src = readSource(SHOWCASE_PATH);
    const light = themedSection(src, 'light');
    const real = realTagsPresent(light);
    assert.ok(
      real.length > 0,
      `light themed View must contain real component tags; found only: ${[
        ...new Set([...light.matchAll(/<([A-Z][A-Za-z]*)\b/g)].map((m) => m[1])),
      ].join(', ')}`,
    );
  });

  it('light section covers representative tiers (bar, button, card)', () => {
    const src = readSource(SHOWCASE_PATH);
    const light = themedSection(src, 'light');
    for (const tag of ['TopAppBar', 'Button', 'Card']) {
      assert.ok(
        light.includes(`<${tag}`),
        `light themed View must contain a <${tag} tag`,
      );
    }
  });
});

describe('T1 dark themed section contains real tags (AC-M3-015)', () => {
  it('dark section renders real M3 tags, not Text stubs only', () => {
    const src = readSource(SHOWCASE_PATH);
    const dark = themedSection(src, 'dark');
    const real = realTagsPresent(dark);
    assert.ok(
      real.length > 0,
      `dark themed View must contain real component tags; found only: ${[
        ...new Set([...dark.matchAll(/<([A-Z][A-Za-z]*)\b/g)].map((m) => m[1])),
      ].join(', ')}`,
    );
  });

  it('dark section covers representative tiers (bar, button, card)', () => {
    const src = readSource(SHOWCASE_PATH);
    const dark = themedSection(src, 'dark');
    for (const tag of ['TopAppBar', 'Button', 'Card']) {
      assert.ok(
        dark.includes(`<${tag}`),
        `dark themed View must contain a <${tag} tag`,
      );
    }
  });
});

describe('T1 real tags moved out of the third unthemed View (AC-M3-015)', () => {
  it('has zero stub-only rows left in the themed sections', () => {
    const src = readSource(SHOWCASE_PATH);
    const themed = themedSection(src, 'light') + '\n' + themedSection(src, 'dark');
    const stubs = stubOnlyRows(themed);
    assert.equal(
      stubs.length,
      0,
      `themed sections must not keep Text stubs; found ${stubs.length}: ${stubs
        .slice(0, 3)
        .map((s) => s.trim())
        .join(' | ')}`,
    );
  });

  it('keeps no third unthemed View hoarding the real tags', () => {
    const src = readSource(SHOWCASE_PATH);
    const plainViews = [...src.matchAll(/<View>/g)].length;
    assert.ok(
      plainViews <= 1,
      `Showcase must not keep a third unthemed <View> of real tags; found ${plainViews} plain <View> opens`,
    );
  });

  it('retains default-exported Showcase() in static-literal form', () => {
    const src = readSource(SHOWCASE_PATH);
    assert.match(src, /export default function Showcase\(\)/, 'must retain default-exported Showcase()');
    assert.ok(!/\buse[A-Z]/.test(src), 'Showcase must stay hook-free (static-literal form)');
  });
});

// --- T2 (AC-M3-016 / FR-M3-007): story rows render the descriptor type ---

describe('T2 story rows render instead of JSON debug dump (AC-M3-016)', () => {
  it('does not JSON.stringify the story render descriptor', () => {
    const src = readSource(APP_PATH);
    assert.ok(
      !src.includes('JSON.stringify(rendered)'),
      'App.tsx must not render the story descriptor via JSON.stringify(rendered)',
    );
  });

  it('renders the story descriptor type as a tag', () => {
    const src = readSource(APP_PATH);
    assert.ok(
      src.includes('rendered.type'),
      'App.tsx story rows must render the descriptor type as a tag (reference rendered.type in tag position)',
    );
  });

  it('keeps the buildStories mapping', () => {
    const src = readSource(APP_PATH);
    assert.match(src, /buildStories\(manifest\)/, 'App.tsx must keep the buildStories(manifest) mapping');
    assert.match(src, /stories\.map\(/, 'App.tsx must keep mapping over stories for rows');
  });

  it('retains default-exported App()', () => {
    const src = readSource(APP_PATH);
    assert.match(src, /export default function App\(\)/, 'must retain default-exported App()');
  });
});
