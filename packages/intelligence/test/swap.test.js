'use strict';
// T1 — Meta-First swap contract (FR-MUSE-001, AC-MUSE-002).
// Hermetic: pure in-memory route fixtures shaped like lens.js bodies
// (matches[].mask, reasoning_content deltas, strict-schema payloads).
// No network, no keys, no outside model/vendor names — the
// non-Muse side is called only "generic provider" (RULE-MUSE-006/008).
// Uses node:test + node:assert, matching lens-reason.test.js style.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { evaluateSwap, checkSwapAnswers } = require('../lib/swap.js');

// --- Fixtures -----------------------------------------------------------

const POLYGON = [[10, 10], [100, 10], [100, 100], [10, 100]];

function museResult() {
  return {
    segment: {
      matches: [
        { mask: { encoding: 'polygon', points: POLYGON }, confidence: 0.92 },
      ],
    },
    reasonDeltas: [
      { reasoning_content: 'Thinking about ' },
      { reasoning_content: 'the kettle…' },
      { content: 'A kettle boils water.' },
    ],
    structure: {
      summary: 'Top complaint: battery drains fast.',
      groups: [
        {
          label: 'Negative',
          sentiment: 'negative',
          items: [{ id: 'r1', text: 'Battery dies in 2 hours.' }],
        },
      ],
    },
  };
}

// Generic-provider fixtures: hermetic fakes, never a named outside model.
function genericResult() {
  return {
    segment: {
      matches: [
        { mask: { encoding: 'run-length', counts: [5, 10, 5] }, confidence: 0.7 },
      ],
    },
    reasonDeltas: [
      { content: 'A kettle boils water.' },
    ],
    structure: {
      text: 'A flat paragraph with no summary/groups/items hierarchy.',
    },
  };
}

// --- Cases --------------------------------------------------------------

describe('evaluateSwap — Meta-First swap contract (AC-MUSE-002)', () => {
  it('polygon viz breaks on generic-provider swap (generic run-length mask)', () => {
    const res = evaluateSwap(museResult(), genericResult());
    assert.equal(res.polygonViz, 'broken');
  });

  it('polygon viz stays ok when the generic fixture is replaced by the Muse fixture (falsifiable)', () => {
    const res = evaluateSwap(museResult(), museResult());
    assert.equal(res.polygonViz, 'ok');
  });

  it('Thinking stream breaks on generic-provider swap (content-only deltas, zero reasoning_content)', () => {
    const res = evaluateSwap(museResult(), genericResult());
    assert.equal(res.thinkingStream, 'broken');
  });

  it('Thinking stream stays ok when the generic fixture is replaced by the Muse fixture (falsifiable)', () => {
    const res = evaluateSwap(museResult(), museResult());
    assert.equal(res.thinkingStream, 'ok');
  });

  it('strict structured output degrades on generic-provider swap (loose payload fails schema validation)', () => {
    const res = evaluateSwap(museResult(), genericResult());
    assert.equal(res.structuredOutput, 'degraded');
  });

  it('structured output stays ok when the generic fixture is replaced by the Muse fixture (falsifiable)', () => {
    const res = evaluateSwap(museResult(), museResult());
    assert.equal(res.structuredOutput, 'ok');
  });

  it('overall is generic-degraded on swap, meta-first when backends match', () => {
    assert.equal(evaluateSwap(museResult(), genericResult()).overall, 'generic-degraded');
    assert.equal(evaluateSwap(museResult(), museResult()).overall, 'meta-first');
  });

  it('boundary: generic empty matches break polygon viz (named breakage, never silent)', () => {
    const generic = genericResult();
    generic.segment = { matches: [] };
    const res = evaluateSwap(museResult(), generic);
    assert.equal(res.polygonViz, 'broken');
    assert.equal(res.overall, 'generic-degraded');
  });
});

// --- Gate ---------------------------------------------------------------
// T3 — spec-entry gate (FR-MUSE-001, AC-MUSE-003).
// Hermetic: the registry below is an in-test literal mirroring each active
// component's spec-entry swap answer (component → breakage string), mirroring
// the docs/meta-first.md section 5 table (T2). No spec-file parsing, no
// filesystem reads. checkSwapAnswers(entries) is pure: it returns `missing`
// for empty/absent answers.

const SWAP_ANSWERS = {
  MuseLens: 'Mask overlay cannot render: generic run-length/box shape is unparseable as polygon points, so segmentation viz breaks.',
  MuseField: 'Thinking indicator goes silent: generic content-only deltas yield zero reasoning_content callbacks, so the field falls back to degraded plain text.',
  MuseList: 'Grouped summary→groups→items hierarchy fails schema validation with a named error and degrades to plain text.',
  MuseVoice: 'Dictation yields no partials/final transcript shape: transcript target stays empty, so voice entry breaks.',
  MuseImage: 'Avatar/cover generation loses identity stability: same prompt re-renders non-reproducibly and style presets are ignored, so fallback art drifts per call.',
};

describe('checkSwapAnswers — spec-entry gate (AC-MUSE-003)', () => {
  it('passes when all five active components record a non-empty swap answer', () => {
    const res = checkSwapAnswers({ ...SWAP_ANSWERS });
    assert.equal(res.ok, true);
    assert.deepEqual(res.missing, []);
  });

  it('gate fails when any entry is removed (absent key → missing)', () => {
    const { MuseVoice, ...withoutVoice } = SWAP_ANSWERS;
    void MuseVoice;
    const res = checkSwapAnswers(withoutVoice);
    assert.equal(res.ok, false);
    assert.deepEqual(res.missing, ['MuseVoice']);
  });

  it('gate fails when any entry is emptied (empty string → missing)', () => {
    const res = checkSwapAnswers({ ...SWAP_ANSWERS, MuseList: '' });
    assert.equal(res.ok, false);
    assert.deepEqual(res.missing, ['MuseList']);
  });

  it('boundary: whitespace-only answer counts as missing', () => {
    const res = checkSwapAnswers({ ...SWAP_ANSWERS, MuseImage: '   ' });
    assert.equal(res.ok, false);
    assert.deepEqual(res.missing, ['MuseImage']);
  });

  it('boundary: null/undefined answer counts as missing, all missing listed', () => {
    const res = checkSwapAnswers({ ...SWAP_ANSWERS, MuseLens: null, MuseField: undefined });
    assert.equal(res.ok, false);
    assert.deepEqual(res.missing, ['MuseLens', 'MuseField']);
  });
});
