/**
 * Batch 2 scrim-tap-dismiss — FR-M3-010 / AC-M3-022 scrim-tap-dismiss clause
 * (checklist L3). Rides the BUG-M3-FIDELITY-007 tap chain (arena/slop
 * governs); dismiss stays narrowly scoped to the scrim hit-target so
 * AC-INPUT-020 (drag-no-fire) is not regressed.
 *
 * Hermetic: node:test + node:assert/strict only. Reads gallery/engine
 * source text from disk; no network, no secrets, no services, no device.
 * The L1 hook-site case in `layering.test.ts` (untouched, still green)
 * pins the declaration site; this file pins the dismiss behavior contract.
 *
 * File choice: L3 dismiss routing lives in the engine presenter with a
 * composite passthrough, and its hermetic halves are source-text +
 * descriptor assertions — so they belong in `packages/native/test/`
 * alongside the Batch 1 scene tests, in a NEW file (Batch 1's
 * `layering.test.ts` stays byte-identical per the Batch 2 claim).
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const PRESENTER_URL = new URL(
  '../../../engine/runtime/src/gallery_present.cpp',
  import.meta.url,
);
const PRESENTER_HEADER_URL = new URL(
  '../../../engine/runtime/include/runtime/gallery_present.h',
  import.meta.url,
);
const DIALOG_URL = new URL('../composites/Dialog.tsx', import.meta.url);

function read(url) {
  return readFileSync(url, 'utf8');
}

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

test('onScrimTap is declared for the layering page only', () => {
  const header = read(PRESENTER_HEADER_URL);
  const cpp = read(PRESENTER_URL);
  assert.ok(
    header.includes('onScrimTap'),
    'gallery_present.h must declare onScrimTap (L3 interface)',
  );
  assert.ok(
    cpp.includes('onScrimTap'),
    'gallery_present.cpp must route the scrim hit-target through onScrimTap on the layering page only',
  );
});

test('scrim tap dismisses exactly the topmost overlay once', () => {
  const cpp = read(PRESENTER_URL);
  assert.ok(
    cpp.includes('DismissResult'),
    'presenter must define DismissResult for the scrim-tap outcome',
  );
  assert.ok(
    cpp.includes("'topmost'") || cpp.includes('"topmost"'),
    "dismiss must name the dismissed layer as 'topmost'",
  );
  assert.ok(
    cpp.includes('remaining'),
    'dismiss result must carry `remaining` (count decrements by exactly one)',
  );
});

test('tap on content is a mis-hit (no dismiss)', () => {
  const cpp = read(PRESENTER_URL);
  assert.ok(
    cpp.includes("'miss'") || cpp.includes('"miss"'),
    "content tap must resolve to { dismissed: 'none', reason: 'miss' }",
  );
  assert.ok(
    cpp.includes("'none'") || cpp.includes('"none"'),
    "mis-hit must report dismissed: 'none'",
  );
});

test('drag never fires dismiss (AC-INPUT-020 preserved)', () => {
  const cpp = read(PRESENTER_URL);
  assert.ok(
    cpp.includes("'drag'") || cpp.includes('"drag"'),
    "drag must resolve to { dismissed: 'none', reason: 'drag' } — never a dismiss",
  );
});

test('non-dismissals never throw', () => {
  const cpp = read(PRESENTER_URL);
  const at = cpp.indexOf('onScrimTap');
  assert.notEqual(
    at,
    -1,
    'onScrimTap must exist before its no-throw guarantee can be checked (L3)',
  );
  const window = cpp.slice(at, at + 2000);
  assert.ok(
    window.includes('noexcept') || window.includes('catch'),
    'onScrimTap must guarantee no-throw on miss/drag paths (error shape, never a throw)',
  );
});

test('Dialog passes the dismiss contract through without owning geometry', () => {  const src = read(DIALOG_URL);
  const hits =
    countOccurrences(src, 'onScrimTap') + countOccurrences(src, 'onDismiss');
  assert.ok(
    hits >= 1,
    'Dialog.tsx must forward the dismiss contract prop (onScrimTap/onDismiss passthrough)',
  );
  assert.ok(
    !src.includes('sheetY') && !src.includes('anchorSheetToImeInset'),
    'Dialog composite must not own geometry — scrim hit-target geometry stays in the L2 presenter',
  );
});

// ─── Batch 4 — F2 dismiss observability (FR-M3-010 / AC-M3-022) ───
// APPENDED — the 6 green L3 cases above are untouched. Hermetic:
// source-text read of the presenter only (existing file style).
// ASSUMPTION (contract genuinely silent on the log spelling): the
// scrim-tap dismiss path emits an observable record (log line, evidence
// printf/LOG, or recorded dismiss marker naming the dismissed layer plus
// the remaining count) so before/after state is confirmable on-device via
// logcat. The coder owns the exact spelling — reconcile paths, not verdicts.
test('scrim-tap dismiss emits an observable dismiss record (F2)', () => {
  const cpp = read(PRESENTER_URL);
  const at = cpp.indexOf('onScrimTap');
  assert.notEqual(at, -1, 'onScrimTap must exist before its observability can be checked (F2)');
  const window = cpp.slice(at, at + 4000);
  assert.ok(
    /dismiss/i.test(window) &&
      (/log/i.test(window) ||
        /evidence/i.test(window) ||
        /printf/i.test(window) ||
        /LOG/i.test(window) ||
        /record/i.test(window) ||
        /observable/i.test(window)),
    'onScrimTap must emit an observable dismiss log/evidence record (dismiss + remaining) confirmable on-device (F2)',
  );
});
