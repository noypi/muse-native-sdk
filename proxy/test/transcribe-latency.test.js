'use strict';
// TC-MUSE-052 — NFR-MUSE-001 / AC-MUSE-050: proxy-leg tap-to-transcript p95 ≤ 2s.
// Hermetic: node:test + node:assert/strict + node:perf_hooks only. In-process
// stub handle, no live proxy, no network, no secrets. Deletes every known key
// env var before each case to prove the gate never needs key custody.
// Contract: the gate is `p95(turnLatenciesMs) <= 2000` over the measured turn
// set against the stub-backed proxy fixture set. The p95 helper lives in this
// test file (nearest-rank); production is not required to export it.
// ASSUMPTION (gate-honest): the stub is synchronous, so live-measured turns
// are microseconds — the gate would pass vacuously. To prove the gate can go
// RED, the injected-latency cases assert a violating sample set IS detected
// (p95 > 2000 fails the gate) and the helper's edge cases are pinned. If the
// live leg ever exceeds budget, the live case fails RED and the coder owns the
// proxy leg (batching, streaming, timeout/queueing), never this file.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const { performance } = require('node:perf_hooks');

const SLO_P95_MS = 2000;
const TURNS = 25;

function loadImpl() {
  return require('../lib/stub.js');
}

// Nearest-rank p95 over a sample of per-turn latencies (ms). Test-local
// helper: defines what the gate means, not production behavior.
function p95(sortedOrUnsorted) {
  const samples = Array.from(sortedOrUnsorted);
  if (samples.length === 0) throw new RangeError('p95 needs at least one sample');
  const sorted = samples.slice().sort((a, b) => a - b);
  const rank = Math.ceil(0.95 * sorted.length);
  return sorted[rank - 1];
}

function passesSlo(samples) {
  return p95(samples) <= SLO_P95_MS;
}

let savedEnv;
beforeEach(() => {
  savedEnv = { ...process.env };
  delete process.env.MUSE_API_KEY;
  delete process.env.XAI_API_KEY;
  delete process.env.OPENAI_API_KEY;
});
afterEach(() => {
  process.env = savedEnv;
});

describe('TC-MUSE-052 — p95 gate semantics (injected latencies)', () => {
  it('passes a sample set entirely under the 2s budget', () => {
    const samples = [120, 240, 310, 180, 900, 1500, 410, 95, 620, 1999,
      300, 275, 510, 1110, 130, 1450, 760, 830, 220, 1990];
    assert.equal(p95(samples) <= SLO_P95_MS, true);
    assert.equal(passesSlo(samples), true);
  });

  it('detects a violating set: p95 above 2s fails the gate (gate can go RED)', () => {
    // 18 fast turns + 2 slow turns in 20 samples: nearest-rank p95
    // (rank 19) lands on a slow turn, so the gate must reject the set.
    const samples = Array(18).fill(150).concat([5000, 6000]);
    assert.ok(p95(samples) > SLO_P95_MS, `p95 must exceed budget, got ${p95(samples)}`);
    assert.equal(passesSlo(samples), false);
  });

  it('uses nearest-rank: p95 of 1..100 is 95', () => {
    const samples = Array.from({ length: 100 }, (_, i) => i + 1);
    assert.equal(p95(samples), 95);
  });

  it('rejects an empty sample set instead of passing vacuously', () => {
    assert.throws(() => p95([]), RangeError);
  });

  it('pins the budget at exactly 2000ms (NFR-MUSE-001)', () => {
    assert.equal(SLO_P95_MS, 2000);
  });
});

describe('TC-MUSE-052 — proxy-leg tap-to-transcript p95 vs stub (AC-MUSE-050)', () => {
  it(`measures ${TURNS} stub transcribe turns with p95 <= 2000ms`, (t) => {
    assert.equal(process.env.MUSE_API_KEY, undefined);
    const stub = loadImpl();
    const latencies = [];
    for (let i = 0; i < TURNS; i += 1) {
      const turnId = `tc052-turn-${i}`;
      const start = performance.now();
      const res = stub.handle({ route: 'transcribe', params: { mode: 'PUSH_TO_TALK' }, turnId });
      const elapsed = performance.now() - start;
      assert.equal(res.status, 200);
      assert.ok(res.body && typeof res.body.final === 'string', 'turn must resolve a transcript final');
      assert.equal(res.body.turnId, turnId, 'turn latency must map to its own turnId');
      latencies.push(elapsed);
    }
    const measured = p95(latencies);
    const max = Math.max(...latencies);
    t.diagnostic(`tap-to-transcript proxy-leg over ${TURNS} stub turns: p95=${measured.toFixed(3)}ms max=${max.toFixed(3)}ms budget=2000ms`);
    assert.ok(measured <= SLO_P95_MS, `p95 ${measured}ms exceeds 2000ms budget`);
  });

  it('each measured turn carries transcript partials then final (tap-to-transcript shape)', () => {
    const stub = loadImpl();
    const res = stub.handle({ route: 'transcribe', params: { mode: 'PUSH_TO_TALK' }, turnId: 'tc052-shape' });
    assert.equal(res.status, 200);
    assert.ok(Array.isArray(res.body.partials) && res.body.partials.length > 0, 'partials must stream before final');
    assert.ok(typeof res.body.final === 'string' && res.body.final.length > 0, 'final transcript must be non-empty');
  });
});
