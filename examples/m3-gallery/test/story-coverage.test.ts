'use strict';
// TC-M3-016 story coverage (AC-M3-016) + T5 gallery generation (AC-M3-015,
// unit-verifiable half). Batch 3 (T5/T6 contract).
//
// Hermetic: node:test + node:assert/strict only. Reads muse.manifest.json,
// the gallery story module, and the gallery package.json from disk; uses
// in-memory synthetic manifests for the add/remove fixtures. No network, no
// secrets, no live services, no device, no shared mutable state.
//
// Binds ONLY to the plan interfaces:
//   T5: `buildStories(manifest): Array<{ component, tier, variant, state,
//       theme: "light"|"dark", render() }>` exported from `src/stories.js`.
//   T6: `npm run test:stories -w @muse/gallery → exit 0`.
// Never binds to an implementation.
//
// ASSUMPTIONS:
//   A1 (requireable stories module): `src/stories.js` is loadable via
//       require() and exports `buildStories` as a pure function of the
//       manifest object. The coder reconciles module format; the behavior
//       below (manifest-driven stories) is what must hold.
//   A2 (values are variants): every `values` list in a manifest prop schema
//       is a documented variant set; each value appears as the `variant` of
//       at least one story per theme, and in that story's render() output.
//   A3 (booleans are states): boolean props named enabled/selected/checked/
//       open/error are interaction states; a component declaring any of them
//       exposes at least two distinct `state` values across its stories.
//   A4 (rendered descriptor): story.render() returns a plain descriptor
//       whose `type` equals the component name and whose JSON form carries
//       the theme and the exercised variant value. Overlay stories carry
//       `OverlayHost` (RULE-M3-006).
//   A5 (App + device are manual): `App()` rendering every story and
//       `muse-native run` (TC-M3-015 procedure) need the renderer/device and
//       are NOT unit-verifiable here; marked UNVERIFIED, owner tester/manual.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const MANIFEST_PATH =
  process.env.MANIFEST_PATH ??
  join(__dirname, '..', '..', '..', 'packages', 'native', 'muse.manifest.json');
const STORIES_PATH =
  process.env.STORIES_PATH ?? join(__dirname, '..', 'src', 'stories.js');
const PACKAGE_PATH =
  process.env.GALLERY_PACKAGE_PATH ?? join(__dirname, '..', 'package.json');

const STATE_PROPS = ['enabled', 'selected', 'checked', 'open', 'error'];
const OVERLAY_COMPONENTS = ['Dialog', 'BottomSheet', 'Menu', 'SnackbarHost', 'Tooltip'];
const THEMES = ['light', 'dark'];

function loadManifest(path = MANIFEST_PATH) {
  return JSON.parse(readFileSync(path, 'utf8'));
}

let storiesError = null;
let buildStories = undefined;
try {
  ({ buildStories } = require(STORIES_PATH));
} catch (err) {
  storiesError = err;
}

function requireBuildStories() {
  assert.equal(
    storiesError,
    null,
    `story module failed to load from ${STORIES_PATH}: ${storiesError && storiesError.message}`,
  );
  assert.equal(
    typeof buildStories,
    'function',
    `story module at ${STORIES_PATH} must export buildStories(manifest)`,
  );
}

function storiesForManifest(manifest) {
  requireBuildStories();
  const stories = buildStories(manifest);
  assert.ok(Array.isArray(stories), 'buildStories(manifest) must return an array');
  return stories;
}

function valuesOf(entry) {
  const out = [];
  for (const [prop, schema] of Object.entries(entry.props ?? {})) {
    if (schema !== null && typeof schema === 'object' && Array.isArray(schema.values)) {
      for (const value of schema.values) out.push({ prop, value });
    }
  }
  return out;
}

function statePropsOf(entry) {
  return Object.keys(entry.props ?? {}).filter(
    (key) => STATE_PROPS.includes(key) && entry.props[key] !== null &&
      typeof entry.props[key] === 'object' && entry.props[key].type === 'boolean',
  );
}

describe('TC-M3-016 — gallery stories are generated from the manifest (AC-M3-016)', () => {
  it('story module exports buildStories(manifest)', () => {
    requireBuildStories();
    assert.ok(buildStories.length >= 1, 'buildStories must take the manifest argument');
  });

  it('buildStories leaves the manifest object untouched (pure function)', () => {
    const manifest = loadManifest();
    const before = JSON.stringify(manifest);
    storiesForManifest(manifest);
    assert.equal(JSON.stringify(manifest), before, 'buildStories must not mutate its input');
  });

  it('every manifest entry has at least one story (names the component)', () => {
    const manifest = loadManifest();
    const stories = storiesForManifest(manifest);
    const covered = new Set(stories.map((s) => s && s.component));
    const missing = manifest.components.map((c) => c.name).filter((n) => !covered.has(n));
    assert.deepEqual(missing, [], `manifest entries with no story: ${missing.join(', ')}`);
  });

  it('every story maps to a manifest entry (names the orphan)', () => {
    const manifest = loadManifest();
    const stories = storiesForManifest(manifest);
    const known = new Set(manifest.components.map((c) => c.name));
    const orphans = stories.map((s) => s && s.component).filter((n) => !known.has(n));
    assert.deepEqual(orphans, [], `stories with no manifest entry: ${orphans.join(', ')}`);
  });

  it('story shape carries component/tier/variant/state/theme/render', () => {
    const manifest = loadManifest();
    const stories = storiesForManifest(manifest);
    const tierByName = new Map(manifest.components.map((c) => [c.name, c.tier]));
    assert.ok(stories.length > 0, 'expected at least one story');
    for (const story of stories) {
      assert.equal(typeof story.component, 'string', 'story.component must be a string');
      assert.equal(story.tier, tierByName.get(story.component), `${story.component}: tier must match manifest`);
      assert.equal(typeof story.variant, 'string', `${story.component}: variant must be a string`);
      assert.equal(typeof story.state, 'string', `${story.component}: state must be a string`);
      assert.ok(THEMES.includes(story.theme), `${story.component}: theme must be light|dark`);
      assert.equal(typeof story.render, 'function', `${story.component}: render must be a function`);
    }
  });

  it('every component is covered in both light and dark themes', () => {
    const manifest = loadManifest();
    const stories = storiesForManifest(manifest);
    const gaps = [];
    for (const entry of manifest.components) {
      for (const theme of THEMES) {
        if (!stories.some((s) => s.component === entry.name && s.theme === theme)) {
          gaps.push(`${entry.name}/${theme}`);
        }
      }
    }
    assert.deepEqual(gaps, [], `component/theme pairs with no story: ${gaps.join(', ')}`);
  });

  it('overlay members render inside OverlayHost (RULE-M3-006)', () => {
    const manifest = loadManifest();
    const stories = storiesForManifest(manifest);
    for (const name of OVERLAY_COMPONENTS) {
      const own = stories.filter((s) => s.component === name);
      assert.ok(own.length > 0, `expected stories for overlay member ${name}`);
      for (const story of own) {
        const rendered = story.render();
        assert.ok(
          JSON.stringify(rendered).includes('OverlayHost'),
          `${name}/${story.theme} story must layer via OverlayHost`,
        );
      }
    }
  });

  it('gallery package exposes the test:stories script (TC-M3-016 check)', () => {
    let pkg = null;
    try {
      pkg = JSON.parse(readFileSync(PACKAGE_PATH, 'utf8'));
    } catch (err) {
      assert.fail(`gallery package.json unreadable at ${PACKAGE_PATH}: ${err.message}`);
    }
    assert.equal(pkg.name, '@muse/gallery', 'gallery package must be named @muse/gallery');
    assert.equal(
      typeof (pkg.scripts && pkg.scripts['test:stories']),
      'string',
      'gallery package.json must define a test:stories script',
    );
  });
});

describe('AC-M3-015 — every documented variant/state renders in both themes (unit half)', () => {
  const manifest = loadManifest();
  const withValues = manifest.components.filter((c) => valuesOf(c).length > 0);

  it('at least one component documents values variants (guard against vacuous pass)', () => {
    requireBuildStories();
    assert.ok(withValues.length > 0, 'expected manifest entries with values lists');
  });

  for (const entry of withValues) {
    it(`${entry.name} covers every documented values variant in light+dark`, () => {
      const stories = storiesForManifest(loadManifest());
      for (const { prop, value } of valuesOf(entry)) {
        for (const theme of THEMES) {
          assert.ok(
            stories.some((s) => s.component === entry.name && s.theme === theme && s.variant === value),
            `${entry.name}: no ${theme} story with variant "${value}" (prop ${prop})`,
          );
        }
      }
    });

    it(`${entry.name} render() returns its own descriptor carrying theme+variant`, () => {
      const stories = storiesForManifest(loadManifest());
      for (const theme of THEMES) {
        const own = stories.filter((s) => s.component === entry.name && s.theme === theme);
        assert.ok(own.length > 0, `${entry.name}: no ${theme} story to render`);
        const rendered = own[0].render();
        assert.equal(rendered && rendered.type, entry.name, `${entry.name}: render().type must equal its name`);
        assert.ok(
          JSON.stringify(rendered).includes(theme),
          `${entry.name}: ${theme} render() output must carry the theme`,
        );
        for (const { prop, value } of valuesOf(entry)) {
          assert.ok(
            own.some((s) => JSON.stringify(s.render()).includes(value)),
            `${entry.name}: no ${theme} render() output carries variant value "${value}" (prop ${prop})`,
          );
        }
      }
    });
  }

  for (const entry of manifest.components.filter((c) => statePropsOf(c).length > 0)) {
    it(`${entry.name} exposes its documented states (${statePropsOf(entry).join(', ')})`, () => {
      const stories = storiesForManifest(loadManifest());
      const states = new Set(
        stories.filter((s) => s.component === entry.name).map((s) => s.state),
      );
      assert.ok(
        states.size >= 2,
        `${entry.name}: expected at least two distinct state values, saw [${[...states].join(', ')}]`,
      );
    });
  }
});

describe('AC-M3-016 — manifest add/remove flows through with no hand-edited story code', () => {
  it('a component added to the manifest appears in the gallery', () => {
    const manifest = loadManifest();
    const added = {
      name: 'SyntheticWidget',
      tier: 3,
      base: 'Surface',
      props: {
        variant: { type: 'string', values: ['alpha', 'beta'] },
        enabled: { type: 'boolean' },
      },
    };
    const extended = {
      ...manifest,
      components: [...manifest.components, added],
    };
    const stories = storiesForManifest(extended);
    const own = stories.filter((s) => s.component === 'SyntheticWidget');
    assert.ok(own.length > 0, 'added manifest entry must produce stories with no hand edit');
    for (const value of ['alpha', 'beta']) {
      for (const theme of THEMES) {
        assert.ok(
          own.some((s) => s.variant === value && s.theme === theme),
          `SyntheticWidget: no ${theme} story with variant "${value}"`,
        );
      }
    }
  });

  it('a component removed from the manifest loses its stories', () => {
    const manifest = loadManifest();
    const victim = manifest.components.find((c) => c.tier === 3);
    assert.ok(victim, 'expected at least one tier-3 entry to remove');
    const stories = storiesForManifest(manifest);
    assert.ok(
      stories.some((s) => s.component === victim.name),
      `precondition: ${victim.name} has stories before removal`,
    );
    const shrunk = {
      ...manifest,
      components: manifest.components.filter((c) => c.name !== victim.name),
    };
    const after = storiesForManifest(shrunk);
    assert.deepEqual(
      after.filter((s) => s.component === victim.name),
      [],
      `removed entry ${victim.name} must have no stories`,
    );
  });

  it('fixture manifests never mutate the production manifest file', () => {
    const manifest = loadManifest();
    assert.ok(
      !manifest.components.some((c) => c.name === 'SyntheticWidget'),
      'production manifest must not contain the SyntheticWidget fixture',
    );
    assert.equal(manifest.components.length, 41, 'production manifest must still hold 41 entries');
  });
});
