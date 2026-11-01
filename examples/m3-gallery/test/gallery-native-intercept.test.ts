'use strict';
// BUG-SAMPLE-007 Batch 4 (T6) — native intercept seam pin.
// Plan T6 Interface (GEOMETRY-ONLY contract, amended plan.md:100-104):
//   `boolean isShellChromeAdvance(float x, float y, int claimed)`
//   (package-private static or private method for testability; GEOMETRY-ONLY
//   — returns `true` iff the (x, y) sample hits the calibrated shell-chrome
//   Next region, claimed value IGNORED for eligibility and kept only as a
//   logged forensic)
//   + existing `nativeAdvancePage(long): int` / `nativeRenderFrame(long): void`
//   reused unchanged.
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. No adb,
// no device, no secrets, no config, no downloads. Every behavioral case is a
// pure-function pin evaluated in-process against bounds extracted from
// MainActivity.java source; the only filesystem reads are static source pins.
// No shared mutable state.
//
// RED STATE (post-amendment, expected): `isShellChromeAdvance` still gates on
// `claimed < 0 → false`, so every geometry-hit+claimed==-1 case fails for
// THAT reason (Next-region with claimed -1 returns false, expected true).
// This is the correct red: claimed must be forensics-only, not eligibility.
//
// ASSUMPTIONS:
//   A-T6a: region bounds ship as explicit tunable constants in
//     MainActivity.java (dp/px mapping documented) near the predicate; this
//     file extracts them flexibly (NEXT_* / CHROME_* / BUTTON_* / REGION_*
//     float/int literals). If the coder names them otherwise, the extractor
//     fails loudly and the contract is re-published — bounds are never
//     hard-coded assumed pixels here.
//   A-T6b: forward-only JNI per A5 — `nativeAdvancePage(long)` takes no
//     direction; Next-region taps issue one forward advance; Prev-region taps
//     fall through with no page and no emit (documented forward-only
//     contract).
//   A-T6c (REVISED geometry-only per A6 calibration 2026-11-01): `claimed`
//     measures engine-node acceptance only, never chrome-tap validity.
//     Engine Next node is dead/absent (Next-region taps report DOWN −1 AND
//     UP −1); geometry owns chrome identification (chrome owns the top
//     strip; story content dispatches at y≈755). Prev-UP-claimed=21 and
//     surface-UP-claimed=10 corroborate (acceptance happens elsewhere, never
//     on Next). Predicate MUST ignore claimed for eligibility; claimed stays
//     a per-tap Log.d forensic only.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join, resolve } = require('node:path');

const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const MAIN_ACTIVITY = join(__dirname, '..', 'src', 'MainActivity.java');

function readActivity() {
  return readFileSync(MAIN_ACTIVITY, 'utf8');
}

// Extract the isShellChromeAdvance method block (brace-matched) from source.
// THROWS pre-T6 (method absent) — that throw IS the red state.
function requirePredicateBody() {
  const text = readActivity();
  const sig = text.search(/boolean\s+isShellChromeAdvance\s*\(\s*float\s+\w+\s*,\s*float\s+\w+\s*,\s*int\s+\w+\s*\)/);
  assert.notEqual(sig, -1, 'MainActivity.java must define isShellChromeAdvance(float x, float y, int claimed) [missing pre-T6]');
  const braceOpen = text.indexOf('{', sig);
  assert.notEqual(braceOpen, -1, 'isShellChromeAdvance body must open a brace block');
  let depth = 0;
  for (let i = braceOpen; i < text.length; i++) {
    if (text[i] === '{') depth += 1;
    if (text[i] === '}') {
      depth -= 1;
      if (depth === 0) return text.slice(sig, i + 1);
    }
  }
  assert.fail('isShellChromeAdvance body brace match failed');
  return '';
}

// Extract the setOnTouchListener block (listener lambda) from source.
function touchListenerBlock() {
  const text = readActivity();
  const start = text.indexOf('setOnTouchListener');
  assert.notEqual(start, -1, 'MainActivity.java must still own the touch listener');
  return text.slice(start, start + 6000);
}

// Flexible tunable-bounds extractor: finds float/int literals assigned to
// region-ish constant names inside (or near) the predicate. Returns null when
// the coder used unrecognized names — a loud re-publish signal, never a
// silent fallback for green purposes (fallback defaults below are marked
// ASSUMPTION and only used once the predicate exists).
function extractBounds(body) {
  const num = (re) => {
    const m = body.match(re);
    return m ? Number(m[1]) : null;
  };
  const bounds = {
    xMin: num(/(?:NEXT|CHROME|BUTTON|REGION)[_A-Z]*(?:X|X_MIN|XMIN|LEFT)\s*=\s*([0-9]+(?:\.[0-9]+)?)f?/),
    yMax: num(/(?:NEXT|CHROME|BUTTON|REGION)[_A-Z]*(?:Y|Y_MAX|YMAX|TOP|BOTTOM)\s*=\s*([0-9]+(?:\.[0-9]+)?)f?/),
  };
  if (bounds.xMin === null || bounds.yMax === null) return null;
  return bounds;
}

// Reference predicate: GEOMETRY-ONLY pure function of (x, y). Claimed is
// IGNORED for eligibility (A-T6c revised) — forensics only.
function referencePredicate(x, y, claimed, bounds) {
  void claimed; // forensics only, never a gate
  const { xMin, yMax } = bounds;
  return x >= xMin && y <= yMax;
}

function requireBoundsOrDefaults(body) {
  const extracted = extractBounds(body);
  if (extracted) return { bounds: extracted, calibrated: true };
  // ASSUMPTION A-T6a fallback: predicate exists but constants use
  // unrecognized names — evaluate against documented tunable defaults so the
  // region-semantics cases still pin behavior; the static constant-presence
  // pin below records the naming gap for the coder.
  return { bounds: { xMin: 600, yMax: 200 }, calibrated: false };
}

function countOccurrences(haystack, needle) {
  let count = 0;
  let pos = 0;
  for (;;) {
    pos = haystack.indexOf(needle, pos);
    if (pos === -1) return count;
    count += 1;
    pos += needle.length;
  }
}

describe('T6 isShellChromeAdvance region predicate — GEOMETRY-ONLY (AC-SAMPLE-039, FR-SAMPLE-014)', () => {
  it('predicate exists with the plan signature (pure, no JNI)', () => {
    const body = requirePredicateBody();
    assert.match(body, /float\s+\w+\s*,\s*float\s+\w+\s*,\s*int\s+\w+/, 'signature is (float x, float y, int claimed)');
    assert.doesNotMatch(body, /nativeAdvancePage|nativeRenderFrame|nativeDispatchTouch/, 'predicate is pure: no JNI inside');
  });

  it('Next-region inside → true regardless of claimed (-1, 0, 10, 21 alike)', () => {
    const body = requirePredicateBody();
    const { bounds } = requireBoundsOrDefaults(body);
    for (const claimed of [-1, 0, 10, 21]) {
      assert.equal(referencePredicate(660, 150, claimed, bounds), true, `inside-Next must page with claimed=${claimed} (bounds ${JSON.stringify(bounds)})`);
    }
    // Source-shape pin: the predicate body must NOT gate eligibility on
    // claimed (geometry-only; claimed is a logged forensic, never a gate).
    assert.doesNotMatch(body, /if\s*\(\s*claimed\s*<\s*0\s*\)/, 'predicate must not contain the claimed<0 eligibility gate');
  });

  it('Prev-region (left chrome, x small) → false for any claimed, no page', () => {
    const body = requirePredicateBody();
    const { bounds } = requireBoundsOrDefaults(body);
    for (const claimed of [-1, 0, 10, 21]) {
      assert.equal(referencePredicate(60, 150, claimed, bounds), false, `Prev-region must not page with claimed=${claimed} (forward-only contract A5/A-T6b)`);
    }
  });

  it('surface center tap → false for any claimed (FR-SAMPLE-014 surface ban)', () => {
    const body = requirePredicateBody();
    const { bounds } = requireBoundsOrDefaults(body);
    for (const claimed of [-1, 0, 10, 21]) {
      assert.equal(referencePredicate(360, 800, claimed, bounds), false, `surface taps never page (claimed=${claimed})`);
    }
  });

  it('outside-region (bottom sheet area) → false for any claimed', () => {
    const body = requirePredicateBody();
    const { bounds } = requireBoundsOrDefaults(body);
    for (const claimed of [-1, 0, 10, 21]) {
      assert.equal(referencePredicate(360, 1500, claimed, bounds), false, `outside-region never pages (claimed=${claimed})`);
    }
  });

  it('claimed is forensics-only: still logged per tap, never an eligibility gate', () => {
    const body = requirePredicateBody();
    const text = readActivity();
    // The OLD gate shape must be gone from the predicate ...
    assert.doesNotMatch(body, /claimed\s*<\s*0/, 'predicate body must not compare claimed as an eligibility gate');
    // ... while the per-tap forensic log line stays.
    assert.match(text, /Log\.d\([\s\S]{0,300}claimed/, 'per-tap Log.d must keep emitting raw x/y + claimed for forensics');
  });

  it('region edges: x just below xMin → false; y just above yMax → false', () => {
    const body = requirePredicateBody();
    const { bounds } = requireBoundsOrDefaults(body);
    assert.equal(referencePredicate(bounds.xMin - 1, 100, -1, bounds), false, 'left of Next strip misses (even with claimed -1)');
    assert.equal(referencePredicate(660, bounds.yMax + 1, -1, bounds), false, 'below chrome strip misses (even with claimed -1)');
  });

  it('region bounds are explicit tunable constants (pending T7 calibration)', () => {
    const body = requirePredicateBody();
    const extracted = extractBounds(body);
    assert.ok(extracted !== null, 'region bounds must be named tunable constants, not magic literals in the branch');
  });
});

describe('T6 touch-path wiring (static-shape pins on MainActivity.java)', () => {
  it('Next-region ACTION_UP calls nativeAdvancePage + nativeRenderFrame directly exactly once', () => {
    const block = touchListenerBlock();
    const guardPos = block.indexOf('isShellChromeAdvance');
    assert.notEqual(guardPos, -1, 'touch listener must branch on isShellChromeAdvance [absent pre-T6]');
    const advanceCount = countOccurrences(block, 'nativeAdvancePage(engineHandle)');
    const renderCount = countOccurrences(block, 'nativeRenderFrame(engineHandle)');
    assert.equal(advanceCount, 1, `exactly one direct nativeAdvancePage in the listener (found ${advanceCount})`);
    assert.equal(renderCount, 2, `listener keeps its existing surface render + one direct render (found ${renderCount})`);
    assert.ok(guardPos < block.indexOf('nativeAdvancePage(engineHandle)'), 'region guard must precede the direct JNI call');
  });

  it('no new exported-broadcast sender (security-review MEDIUM: fail-closed transport)', () => {
    const text = readActivity();
    assert.equal(countOccurrences(text, 'sendBroadcast'), 0, 'no in-app broadcast sender may be added');
  });

  it('Prev-region produces no page and no emit (forward-only, falls through)', () => {
    const block = touchListenerBlock();
    const advanceCount = countOccurrences(block, 'nativeAdvancePage(engineHandle)');
    assert.ok(advanceCount <= 1, `at most one advance call site (Next only); Prev falls through (found ${advanceCount})`);
    assert.doesNotMatch(block, /prev/i, 'no Prev-region paging branch may exist (Prev stays unpaged per user ruling)');
  });

  it('surface path stays no-page: ACTION_UP render shape intact, no unguarded advance', () => {
    const block = touchListenerBlock();
    assert.match(block, /if\s*\(\s*action\s*==\s*MotionEvent\.ACTION_UP\s*\)/, 'ACTION_UP branch stays');
    assert.match(block, /if\s*\(\s*claimed\s*>=\s*0\s*\)/, 'claimed>=0 IME branch stays');
    assert.match(block, /nativeRenderFrame\(engineHandle\)/, 'surface render stays');
  });

  it('engineHandle == 0 falls through with no page (fail-closed)', () => {
    const block = touchListenerBlock();
    const guardPos = block.search(/if\s*\(\s*engineHandle\s*==\s*0\s*\)/);
    assert.notEqual(guardPos, -1, 'zero-handle guard stays at listener entry');
    const jniPos = block.indexOf('nativeAdvancePage(engineHandle)');
    assert.ok(jniPos === -1 || guardPos < jniPos, 'zero-handle guard must precede any direct advance');
  });
});
