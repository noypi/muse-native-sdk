'use strict';
// BUG-MUSE-004 tap path — shelf + engine plumbing pins (TC-MUSE-044 tap half).
// support.md: demo rows render as bare <Text> with no touch handler, and the
// C++ ListItem branch paints only (circle + caption, no hit-test/selection).
// These tests pin the two outer layers of the tap chain by reading the
// shipping sources as data; every one is RED until the coder plumbs the
// event (missing handler / paint-only branch).
//
// Hermetic: node:test + node:assert/strict only. Reads
// examples/muse-demo/src/listShelf.tsx and
// engine/runtime/src/gallery_present.cpp from disk as text — no build, no
// device, no network, no secrets, no services. Assertions are rename-tolerant
// (accept onPress or onTouchStart; tap/Tap/hit/Hit/select/Select/onPress)
// so the coder's choice of tap primitive does not break the contract, only
// the absence of any tap plumbing does.
//
// ASSUMPTION PRIMITIVE-1: support.md recommends `onPress` for consistency
// with Button while the demo switcher rows use `onTouchStart`. Either
// spelling satisfies these tests; the behavior pinned is "each record row
// carries a tap/press handler that delivers its record.id".

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const path = require('node:path');

const REPO = path.resolve(__dirname, '..', '..', '..');
const SHELF = path.join(REPO, 'examples', 'muse-demo', 'src', 'listShelf.tsx');
const PRESENTER = path.join(REPO, 'engine', 'runtime', 'src', 'gallery_present.cpp');

function read(p) {
  return readFileSync(p, 'utf8');
}

// ---------------------------------------------------------------------------
// TAP-3 — demo record rows carry a tap handler delivering record.id
// ---------------------------------------------------------------------------

describe('TAP-3: listShelf record rows are tappable (AC-MUSE-044)', () => {
  it('record-row rendering carries a tap/press handler (no bare <Text> rows)', () => {
    const src = read(SHELF);
    const recordMap = src.slice(src.indexOf('group.records.map'));
    assert.ok(
      recordMap.length > 0 && src.includes('group.records.map'),
      'the shelf must still render rows via group.records.map',
    );
    assert.ok(
      /onTouchStart|onPress/.test(recordMap),
      'each record row must carry onTouchStart or onPress — bare <Text> rows are a silent no-op',
    );
  });

  it('the row tap handler delivers the tapped record id', () => {
    const src = read(SHELF);
    const recordMap = src.slice(src.indexOf('group.records.map'));
    assert.ok(
      /record\.id/.test(recordMap) && /onTouchStart|onPress/.test(recordMap),
      'the row handler must close over record.id so the tap reaches selectRow with the right rowId',
    );
  });
});

// ---------------------------------------------------------------------------
// TAP-4 — C++ ListItem branch hit-tests taps and dispatches selection
// ---------------------------------------------------------------------------

describe('TAP-4: C++ ListItem branch handles taps (AC-MUSE-044/045)', () => {
  it('ListItem branch goes beyond paint-only (hit-test or tap dispatch)', () => {
    const src = read(PRESENTER);
    const at = src.indexOf('"ListItem"');
    assert.ok(at > 0, 'presenter must still contain a ListItem branch');
    const branch = src.slice(at, at + 2000);
    assert.ok(
      /[Tt]ap|[Hh]it|[Ss]elect|onPress/.test(branch),
      'ListItem branch must hit-test taps / dispatch selection — paint-only (FillCircle + PaintCaption, return) drops every tap',
    );
  });

  it('unknown-row taps from the engine surface the named error path', () => {
    const src = read(PRESENTER);
    assert.ok(
      /unknown-row|unknown row|UnknownRow|kUnknownRow/i.test(src),
      'the engine must surface the unknown-row named error for taps on stale ids instead of dropping them',
    );
  });
});
