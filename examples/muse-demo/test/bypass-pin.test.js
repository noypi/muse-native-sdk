'use strict';
// T1 — RED bypass pins (GR-08 T1.1, bug-sample-008 Batch 1).
// Proves `annotatePhoto`/`segmentBody`/`guessMask` resolve with zero route
// dispatch on current code, so these tests FAIL today (bypass exists) and
// pass only after the T3/T4 rewire through the stub `segment` route.
// Hermetic: node:test + node:assert/strict only; reads demo sources as text,
// no device, no downloads, no secrets, no config, no network.
//
// Plan Interface: `assertNoRouteCall(fn): void` — fails when the named path
// resolves without route dispatch. Error shape: `BYPASS_PIN_FAILED(path)`.
// Spec refs: AC-SAMPLE-023, AC-SAMPLE-033.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const SRC = join(__dirname, '..', 'src');

function readSrc(name) {
  return readFileSync(join(SRC, name), 'utf8');
}

// Interface under test. Throws BYPASS_PIN_FAILED(path) when the source at
// `path` still carries any local-compute marker (i.e. it resolves without a
// `segment` route dispatch). Returns void once the rewire removes them.
function assertNoRouteCall(path, src, localMarkers) {
  const hits = localMarkers.filter((re) => re.test(src));
  if (hits.length > 0) {
    throw new Error(
      `BYPASS_PIN_FAILED(${path}): resolves via local compute (${hits.map(String).join('; ')}), no segment route dispatch`,
    );
  }
}

describe('T1 — annotatePhoto resolves without route dispatch (bypass pin)', () => {
  it('suggestions.ts carries no local segment geometry (segmentBody/ringAround)', () => {
    const src = readSrc('suggestions.ts');
    assertNoRouteCall('suggestions.ts', src, [
      /function segmentBody/,
      /function ringAround/,
      /ringAround\(/,
    ]);
  });

  it('suggestions.ts dispatches annotatePhoto through the segment route', () => {
    const src = readSrc('suggestions.ts');
    assert.match(
      src,
      /['"]segment['"]/,
      'annotatePhoto must dispatch via the \'segment\' route (stub proxy), not local geometry',
    );
  });
});

describe('T1 — guess round resolves without route dispatch (bypass pin)', () => {
  it('guessMask.ts carries no inline stub fixtures (STUB_MASKS/STUB_REASONS)', () => {
    const src = readSrc('guessMask.ts');
    assertNoRouteCall('guessMask.ts', src, [/STUB_MASKS/, /STUB_REASONS/]);
  });

  it('guessMask.ts resolves the round through the segment route', () => {
    const src = readSrc('guessMask.ts');
    assert.match(
      src,
      /['"]segment['"]/,
      'guess round must resolve via the \'segment\' route (stub proxy), not inline fixtures',
    );
  });
});
