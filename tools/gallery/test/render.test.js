'use strict';
// T11 — FR-DX-007 (AC-DX-029): gallery renderer — light/dark × ltr/rtl with
// props, resolved tokens and a11y semantics visible per page, no per-component
// hand edits. Hermetic: node:test + node:assert/strict + stdlib only. No
// network, no secrets, no application config, no live services, no
// filesystem, no clock.
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T11.md):
// - A-T11a (plan A4): registry entries are `{ name, props, tokens, a11y }`
//   stubs. `props`/`tokens`/`a11y` are plain objects; the coder must accept
//   at least that shape (extra fields ignored, never required).
// - A-T11b: a GalleryPage exposes at least `{ name, props, tokens, a11y,
//   theme, dir }` where `theme`/`dir` echo `opts`, and `props`/`tokens`/
//   `a11y` carry the entry's content (same keys/values, representation is
//   the coder's choice — tests pin key/value visibility, not markup).
// - A-T11c: empty registry yields `[]`; non-array registry or bad
//   theme/dir throws TypeError.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { renderGallery } = require('../lib/gallery.js');

function makeRegistry() {
  return [
    {
      name: 'Button',
      props: { label: 'string', disabled: 'boolean' },
      tokens: { colorPrimary: '#111111', spacingMd: '16px' },
      a11y: { role: 'button', label: 'label' },
    },
    {
      name: 'TextField',
      props: { value: 'string', placeholder: 'string' },
      tokens: { colorSurface: '#ffffff', spacingSm: '8px' },
      a11y: { role: 'textbox', label: 'value' },
    },
  ];
}

const COMBOS = [
  { theme: 'light', dir: 'ltr' },
  { theme: 'light', dir: 'rtl' },
  { theme: 'dark', dir: 'ltr' },
  { theme: 'dark', dir: 'rtl' },
];

function pageText(page) {
  return JSON.stringify(page);
}

describe('renderGallery — one page per registry entry', () => {
  it('returns one page per registry entry', () => {
    const pages = renderGallery(makeRegistry(), { theme: 'light', dir: 'ltr' });
    assert.equal(pages.length, 2);
    assert.deepEqual(
      pages.map((p) => p.name).sort(),
      ['Button', 'TextField'],
    );
  });

  it('echoes the requested theme and dir on every page', () => {
    for (const opts of COMBOS) {
      const pages = renderGallery(makeRegistry(), opts);
      for (const page of pages) {
        assert.equal(page.theme, opts.theme);
        assert.equal(page.dir, opts.dir);
      }
    }
  });
});

describe('renderGallery — props, tokens and a11y visible in all four combos', () => {
  for (const opts of COMBOS) {
    it(`shows props, tokens and a11y (${opts.theme}/${opts.dir})`, () => {
      const pages = renderGallery(makeRegistry(), opts);
      assert.equal(pages.length, 2);
      for (const page of pages) {
        const entry = makeRegistry().find((e) => e.name === page.name);
        const text = pageText(page);
        for (const key of Object.keys(entry.props)) {
          assert.match(text, new RegExp(key), `props key ${key} visible`);
        }
        for (const [key, value] of Object.entries(entry.tokens)) {
          assert.match(text, new RegExp(key), `token key ${key} visible`);
          assert.match(text, new RegExp(String(value).replace(/[#.]/g, '\\$&')), `token value ${value} visible`);
        }
        for (const [key, value] of Object.entries(entry.a11y)) {
          assert.match(text, new RegExp(String(value)), `a11y ${key}=${value} visible`);
        }
      }
    });
  }

  it('resolves dark-theme tokens differently from light-theme tokens', () => {
    const light = renderGallery(makeRegistry(), { theme: 'light', dir: 'ltr' });
    const dark = renderGallery(makeRegistry(), { theme: 'dark', dir: 'ltr' });
    assert.equal(light.length, dark.length);
    const differs = light.some((page, i) => pageText(page) !== pageText(dark[i]));
    assert.equal(differs, true, 'dark variant must differ from light variant');
  });
});

describe('renderGallery — boundary and error paths', () => {
  it('returns [] for an empty registry', () => {
    assert.deepEqual(renderGallery([], { theme: 'light', dir: 'ltr' }), []);
  });

  it('throws TypeError for a non-array registry', () => {
    assert.throws(() => renderGallery(null, { theme: 'light', dir: 'ltr' }), TypeError);
    assert.throws(() => renderGallery({ name: 'Button' }, { theme: 'light', dir: 'ltr' }), TypeError);
  });

  it('throws TypeError for an invalid theme or dir', () => {
    assert.throws(() => renderGallery(makeRegistry(), { theme: 'sepia', dir: 'ltr' }), TypeError);
    assert.throws(() => renderGallery(makeRegistry(), { theme: 'light', dir: 'ttb' }), TypeError);
  });

  it('does not pollute the prototype for a __proto__ token key', () => {
    const pages = renderGallery(
      [{ name: 'X', props: {}, tokens: JSON.parse('{"__proto__":{"polluted":true}}'), a11y: {} }],
      { theme: 'light', dir: 'ltr' },
    );
    const tokens = pages[0].tokens;
    assert.equal(tokens.polluted, undefined);
    assert.equal(Object.getPrototypeOf(tokens), Object.prototype);
    assert.equal({}.polluted, undefined);
  });
});
