'use strict';
// T5 — fixture-mirror removal pins (GR-08 T1.1, bug-sample-008 Batch 3).
// Proves the five fixture-mirror second sources are gone (stub is the single
// source of truth) and the recorded-snapshot fallback in `guessMask.ts` is
// gone-or-spec-blessed. Every test below FAILS today (mirrors survive) and
// passes only after the T5 rewire (or a specs-creator direction-(b) blessing,
// which must narrow the pinned marker, never delete the test).
// Hermetic: node:test + node:assert/strict only; reads demo sources as text,
// no device, no downloads, no secrets, no config, no network.
//
// Plan Interface: none — deletion of mirrored constants/comments; all content
// via route/stub fixtures. Error shape: `MIRROR_PIN_FAILED(path)`.
// Spec refs: AC-SAMPLE-023, AC-SAMPLE-026.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const SRC = join(__dirname, '..', 'src');

const T5_FILES = ['avatar.tsx', 'profile.ts', 'transcribe.ts', 'suggestions.ts', 'onboarding.tsx'];

function readSrc(name) {
  return readFileSync(join(SRC, name), 'utf8');
}

// Interface under test. Throws MIRROR_PIN_FAILED(path) when the source at
// `path` still carries any fixture-mirror marker (a second source of stub
// truth the T5 rewire must delete). Returns void once the mirrors are gone.
function assertNoMirror(path, src, mirrorMarkers) {
  const hits = mirrorMarkers.filter((re) => re.test(src));
  if (hits.length > 0) {
    throw new Error(
      `MIRROR_PIN_FAILED(${path}): fixture-mirror second source survives (${hits.map(String).join('; ')})`,
    );
  }
}

describe('T5 — avatar.tsx carries no stub fixture-mirror', () => {
  it('no mirrors-the-stub request/shape comments', () => {
    const src = readSrc('avatar.tsx');
    assertNoMirror('avatar.tsx', src, [/mirrors the stub/i, /mirroring the stub/i]);
  });
});

describe('T5 — profile.ts carries no stub fixture-mirror', () => {
  it('no mirrors-the-stub reason-route binding comment', () => {
    const src = readSrc('profile.ts');
    assertNoMirror('profile.ts', src, [/mirrors the stub/i]);
  });
});

describe('T5 — transcribe.ts carries no stub fixture-mirror', () => {
  it('no stub-fixture marker / mirroring-the-stub comments', () => {
    const src = readSrc('transcribe.ts');
    assertNoMirror('transcribe.ts', src, [/Stub-fixture markers/i, /mirroring the stub/i]);
  });
});

describe('T5 — suggestions.ts carries no stub-fixture echo', () => {
  it('no stub-fixture authored-against / re-implement / simulated-fixture comments', () => {
    const src = readSrc('suggestions.ts');
    assertNoMirror('suggestions.ts', src, [
      /stub fixtures/i,
      /its fixtures/i,
      /\(simulated\) fixture/i,
    ]);
  });
});

describe('T5 — onboarding.tsx carries no stub fixture-mirror', () => {
  it('no avatar-mirrors / Mirrors-the cross-module fixture comments', () => {
    const src = readSrc('onboarding.tsx');
    assertNoMirror('onboarding.tsx', src, [/avatar mirrors/i, /Mirrors the/i]);
  });
});

describe('T5 — guessMask.ts recorded snapshot is gone-or-spec-blessed', () => {
  it('no RECORDED_* snapshot constants (segment/suggestion/reason)', () => {
    const src = readSrc('guessMask.ts');
    assertNoMirror('guessMask.ts', src, [
      /RECORDED_MATCH_MASK/,
      /RECORDED_SUGGESTION_FIRST/,
      /RECORDED_REASON_MASKED/,
    ]);
  });

  it('no import-less recorded fallback (recordedSegmentBody / mirrors-its-recorded)', () => {
    const src = readSrc('guessMask.ts');
    assertNoMirror('guessMask.ts', src, [/function recordedSegmentBody/, /mirrors its recorded/]);
  });
});

describe('T5 — global gate: zero mirrors-the-stub comments across the five files', () => {
  it('no mirror(s|ing) the stub in avatar/profile/transcribe/suggestions/onboarding', () => {
    const combined = T5_FILES.map(readSrc).join('\n');
    assertNoMirror('T5_FILES', combined, [/mirror(?:s|ing) the stub/i]);
  });
});
