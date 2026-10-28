'use strict';
// T1 — Meta-First swap contract (FR-MUSE-001, AC-MUSE-002).
// Hermetic: pure in-memory route fixtures shaped like lens.js bodies
// (matches[].mask, reasoning_content deltas, strict-schema payloads).
// No network, no keys, no outside model/vendor names — the
// non-Muse side is called only "generic provider" (RULE-MUSE-006/008).
// Uses node:test + node:assert, matching lens-reason.test.js style.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { evaluateSwap } = require('../lib/swap.js');

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
