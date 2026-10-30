/**
 * Batch 1 layering scene — FR-M3-010 / AC-M3-022 / RULE-M3-006 (checklist L1).
 *
 * Hermetic: node:test + node:assert/strict only. Reads gallery/showcase
 * source text from disk and the package public entry (index.js); no network,
 * no secrets, no services, no device.
 *
 * ASSUMPTION S1 (scene location): `LayeringPage` is authored in
 *   `examples/m3-gallery/src/Showcase.tsx` as `export function LayeringPage()`
 *   and registered as a story entry from `examples/m3-gallery/src/App.tsx`.
 * ASSUMPTION S2 (submission order): the five overlays submit in the plan
 *   Interface listing order — Dialog, BottomSheet, Menu, SnackbarHost,
 *   Tooltip — with the scrim submitted before all five.
 * ASSUMPTION S3 (dismiss hook site): the static scene declares the scrim
 *   dismiss hook (`onScrimTap`, owned and implemented by L3) exactly once;
 *   this batch pins the hook site and the DismissResult shape, not behavior.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const SHOWCASE_URL = new URL(
  '../../../examples/m3-gallery/src/Showcase.tsx',
  import.meta.url,
);
const APP_URL = new URL('../../../examples/m3-gallery/src/App.tsx', import.meta.url);
const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);
const BOTTOM_SHEET_URL = new URL('../composites/BottomSheet.tsx', import.meta.url);

function read(url) {
  return readFileSync(url, 'utf8');
}

// LayeringPage body: text between `export function LayeringPage(` and the
// matching end of file scan below. The body extractor takes everything from
// the declaration to the next top-level `export` (or EOF); tests assert on
// that slice so flat-row OverlayHost instances elsewhere do not leak in.
function layeringBody(src) {
  const start = src.indexOf('export function LayeringPage(');
  assert.notEqual(start, -1, 'Showcase.tsx must export LayeringPage (L1)');
  const rest = src.slice(start);
  const nextExport = rest.slice(1).search(/\nexport\s+(function|const|default)/);
  return nextExport === -1 ? rest : rest.slice(0, nextExport + 1);
}

const FIVE_OVERLAYS = ['Dialog', 'BottomSheet', 'Menu', 'SnackbarHost', 'Tooltip'];

function countOccurrences(haystack, needle) {
  let count = 0;
  let from = 0;
  for (;;) {
    const i = haystack.indexOf(needle, from);
    if (i === -1) return count;
    count += 1;
    from = i + needle.length;
  }
}

test('LayeringPage is exported from Showcase.tsx with no props', () => {
  const src = read(SHOWCASE_URL);
  assert.ok(
    src.includes('export function LayeringPage()'),
    'Showcase.tsx must declare `export function LayeringPage()` — no props',
  );
});

test('LayeringPage body composes all five overlays', () => {
  const body = layeringBody(read(SHOWCASE_URL));
  for (const name of FIVE_OVERLAYS) {
    assert.ok(
      body.includes(`<${name}`),
      `LayeringPage must render <${name} in its OverlayHost scene`,
    );
  }
});

test('LayeringPage uses exactly one OverlayHost (RULE-M3-006)', () => {
  const body = layeringBody(read(SHOWCASE_URL));
  assert.equal(
    countOccurrences(body, '<OverlayHost'),
    1,
    'the layering scene must stack all five overlays on exactly one OverlayHost',
  );
});

test('scrim is submitted before all five overlay contents', () => {
  const body = layeringBody(read(SHOWCASE_URL));
  const scrimAt = body.search(/scrim/i);
  assert.notEqual(scrimAt, -1, 'LayeringPage must declare the dimming scrim');
  for (const name of FIVE_OVERLAYS) {
    const at = body.indexOf(`<${name}`);
    assert.ok(scrimAt < at, `scrim must be submitted before <${name}`);
  }
});

test('five overlays submit in Interface listing order', () => {
  const body = layeringBody(read(SHOWCASE_URL));
  const positions = FIVE_OVERLAYS.map((name) => body.indexOf(`<${name}`));
  for (let i = 1; i < positions.length; i += 1) {
    assert.ok(
      positions[i - 1] < positions[i],
      `${FIVE_OVERLAYS[i - 1]} must be submitted before ${FIVE_OVERLAYS[i]}`,
    );
  }
});

test('App.tsx registers the layering story entry', () => {
  const src = read(APP_URL);
  assert.ok(
    src.includes('LayeringPage'),
    'App.tsx must register the LayeringPage story entry',
  );
});

test('layering scene declares the scrim dismiss hook site (L3 contract)', () => {
  const body = layeringBody(read(SHOWCASE_URL));
  const hits = countOccurrences(body, 'onScrimTap') + countOccurrences(body, 'onDismiss');
  assert.equal(
    hits,
    1,
    'LayeringPage must declare exactly one scrim dismiss hook (onScrimTap, implemented by L3)',
  );
});

test('LayeringPage BottomSheet usage satisfies the L4 imeSafe/insetSource contract', () => {
  const body = layeringBody(read(SHOWCASE_URL));
  const match = body.match(/<BottomSheet[^>]*>/);
  assert.ok(match, 'LayeringPage must render <BottomSheet> in its OverlayHost scene');
  const tag = match[0];
  assert.ok(
    tag.includes('imeSafe'),
    'LayeringPage <BottomSheet> must carry the imeSafe prop (L4 contract)',
  );
  assert.ok(
    tag.includes('insetSource') && tag.includes('FR-LAYOUT-012'),
    'LayeringPage <BottomSheet> must carry insetSource="FR-LAYOUT-012" (L4 contract)',
  );
});

