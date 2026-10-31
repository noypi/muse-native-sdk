'use strict';
// T61 — Showcase exposes per-state rows as static literal props.
// Binds ONLY to the plan T61 item:
//   Files: `examples/m3-gallery/src/Showcase.tsx`
//   Interface: none — static JSX tree asset, no callable surface; test
//     surface is the parsed `GalleryRow.desc` containing a literal
//     `state="hover"|"focus"|"pressed"|"disabled"` token per state-variant row.
//   Acceptance: parsed rows contain ≥1 row per wired component × each of the
//     4 states × light/dark with `state=<value>` in desc; token gate still
//     exits 0; no new literal colour/dimension in the diff.
// Spec refs: FR-M3-009 / AC-M3-020 (TC-M3-021); FR-M3-008 / AC-M3-019 (token-only).
// Hermetic: node:test + node:assert/strict + node:fs only. Parses the committed
// source text; never renders, never touches a device, network, secrets, or config.
// ASSUMPTIONS:
//   A1 (D1-disabled/context): disabled opacity numerics are owned by T62
//       (C++ map, 0.38 baseline pin); this file only requires the literal
//       `state="disabled"` token to be present, never a numeric.
//   A2 (wired set): "wired component" = every interactive primitive the T2
//       conformance table names; this test enforces state×theme row presence
//       generically (each state appears in each theme section) plus explicit
//       rows for the T2-wired core (Surface, Ripple, TextFieldEditCore), so a
//       conformance-table addition is covered without editing this file.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const GALLERY_DIR = join(__dirname, '..');
const SHOWCASE_PATH = join(GALLERY_DIR, 'src', 'Showcase.tsx');

const STATES = ['hover', 'focus', 'pressed', 'disabled'];
const THEMES = ['light', 'dark'];
// ASSUMPTION A2: T2-wired interactive core every state row must name.
const WIRED_CORE = ['Surface', 'Ripple', 'TextFieldEditCore'];

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

// All `desc="..."` / `desc='...'` literals in source order.
function galleryDescs(src) {
  const out = [];
  const re = /desc\s*=\s*("([^"]*)"|'([^']*)')/g;
  let m = null;
  while ((m = re.exec(src)) !== null) out.push(m[2] !== undefined ? m[2] : m[3]);
  return out;
}

// All literal `state="..."` tokens.
function stateLiterals(src) {
  const out = [];
  const re = /state\s*=\s*("([^"]*)"|'([^']*)')/g;
  let m = null;
  while ((m = re.exec(src)) !== null) out.push(m[2] !== undefined ? m[2] : m[3]);
  return out;
}

// Split the source at the light/dark section markers (`theme=` pattern).
function themeSections(src) {
  const lightIdx = src.search(/theme\s*=\s*("light"|'light'|\{\s*"light"\s*\})/);
  const darkIdx = src.search(/theme\s*=\s*("dark"|'dark'|\{\s*"dark"\s*\})/);
  assert.ok(lightIdx >= 0, 'light theme section (theme="light") must exist');
  assert.ok(darkIdx >= 0, 'dark theme section (theme="dark") must exist');
  assert.ok(lightIdx !== darkIdx, 'light and dark sections must be distinct');
  const first = Math.min(lightIdx, darkIdx);
  const second = Math.max(lightIdx, darkIdx);
  return {
    light: lightIdx < darkIdx ? src.slice(first, second) : src.slice(second),
    dark: lightIdx < darkIdx ? src.slice(second) : src.slice(first, second),
  };
}

describe('T61 — state-variant rows exist as literal state props (AC-M3-020)', () => {
  it('Showcase.tsx exists and declares GalleryRow rows', () => {
    const src = readShowcase();
    assert.ok(/GalleryRow/.test(src), 'Showcase.tsx must declare GalleryRow rows');
    assert.ok(galleryDescs(src).length > 0, 'GalleryRow rows must carry desc literals');
  });

  for (const state of STATES) {
    it(`at least one row carries the literal state="${state}" token`, () => {
      const src = readShowcase();
      assert.ok(
        stateLiterals(src).includes(state),
        `no GalleryRow with literal state="${state}" in Showcase.tsx`,
      );
    });
  }

  for (const theme of THEMES) {
    for (const state of STATES) {
      it(`theme "${theme}" section contains a state="${state}" row`, () => {
        const src = readShowcase();
        const section = themeSections(src)[theme];
        const lits = stateLiterals(section);
        assert.ok(
          lits.includes(state),
          `theme "${theme}" section has no literal state="${state}" row`,
        );
      });
    }
  }

  for (const name of WIRED_CORE) {
    for (const state of STATES) {
      it(`wired component ${name} has a state="${state}" row (ASSUMPTION A2)`, () => {
        const src = readShowcase();
        const descs = galleryDescs(src);
        assert.ok(
          descs.some((d) => d.includes(name) && d.includes(`state="${state}"`)),
          `${name}: no GalleryRow.desc naming the component with state="${state}"`,
        );
      });
    }
  }

  it('every state="..." literal is a known M3 state token (fail-closed unknown)', () => {
    const src = readShowcase();
    for (const lit of stateLiterals(src)) {
      assert.ok(
        STATES.includes(lit),
        `unknown state literal state="${lit}" must fail the gate (known: ${STATES.join(', ')})`,
      );
    }
  });

  it('empty state literal fails the gate (boundary)', () => {
    const src = readShowcase();
    const empties = stateLiterals(src).filter((s) => s.length === 0);
    assert.equal(empties.length, 0, 'empty state="" literal must not exist');
  });
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
