'use strict';
// T3a — Incremental edit-build cycle gate (NFR-BUILD-011 / AC-BUILD-032;
// repoints TC-BUILD-032; second half of TC-BUILD-035: single-file edit
// rebuilds only dependents).
// Hermetic: node:test + node:assert/strict only. Pure in-memory model —
// synthetic dep graphs and change sets; no filesystem, no network, no
// secrets, no live services.
// Red state: ./incrementalCycle.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim, planRebuild half):
//   planRebuild({changedFiles:string[], depGraph:Record<string,string[]>, cacheHits:Record<string,boolean>}): {steps:string[], cacheHitRate:number}

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./incrementalCycle.js');
}

function smallGraph() {
  return {
    'lib.cc': ['app.cc'],
    'app.cc': [],
    'util.cc': [],
  };
}

describe('T3a — contract shape (AC-BUILD-032)', () => {
  it('returns { steps: string[], cacheHitRate: number } for an empty change set', () => {
    const { planRebuild } = loadImpl();
    const result = planRebuild({ changedFiles: [], depGraph: smallGraph(), cacheHits: {} });
    assert.ok(Array.isArray(result.steps));
    assert.equal(typeof result.cacheHitRate, 'number');
    assert.deepEqual(result.steps, []);
  });

  it('cacheHitRate stays within [0, 1]', () => {
    // ASSUMPTION: hit rate is a ratio in [0,1]; empty change set with no
    // recorded misses reports 1 (fully cached no-op).
    const { planRebuild } = loadImpl();
    const result = planRebuild({ changedFiles: [], depGraph: smallGraph(), cacheHits: {} });
    assert.ok(result.cacheHitRate >= 0 && result.cacheHitRate <= 1);
    assert.equal(result.cacheHitRate, 1);
  });
});

describe('T3a — single-file edit rebuilds only dependents (AC-BUILD-032, TC-BUILD-035)', () => {
  it('edit to lib.cc rebuilds lib.cc and its dependent app.cc, not util.cc', () => {
    const { planRebuild } = loadImpl();
    const result = planRebuild({
      changedFiles: ['lib.cc'],
      depGraph: smallGraph(),
      cacheHits: { 'util.cc': true },
    });
    assert.ok(result.steps.includes('lib.cc'), `steps must include lib.cc, got: ${JSON.stringify(result.steps)}`);
    assert.ok(result.steps.includes('app.cc'), `steps must include dependent app.cc, got: ${JSON.stringify(result.steps)}`);
    assert.ok(!result.steps.includes('util.cc'), `steps must not include untouched util.cc, got: ${JSON.stringify(result.steps)}`);
  });

  it('edit to a leaf file rebuilds only that file', () => {
    const { planRebuild } = loadImpl();
    const result = planRebuild({
      changedFiles: ['util.cc'],
      depGraph: smallGraph(),
      cacheHits: {},
    });
    assert.deepEqual([...result.steps].sort(), ['util.cc']);
  });

  it('full-change fallback: every file changed rebuilds every node', () => {
    const { planRebuild } = loadImpl();
    const graph = smallGraph();
    const result = planRebuild({
      changedFiles: ['lib.cc', 'app.cc', 'util.cc'],
      depGraph: graph,
      cacheHits: {},
    });
    assert.deepEqual([...result.steps].sort(), Object.keys(graph).sort());
  });
});

describe('T3a — fail-closed hardening', () => {
  it('missing input throws fail-closed', () => {
    // ASSUMPTION: plan names no error shape for planRebuild; fail-closed
    // throw is the most defensible behavior for a missing change set.
    const { planRebuild } = loadImpl();
    assert.throws(() => planRebuild(undefined), /./);
    assert.throws(() => planRebuild(null), /./);
  });

  it('missing depGraph throws fail-closed', () => {
    const { planRebuild } = loadImpl();
    assert.throws(() => planRebuild({ changedFiles: ['lib.cc'] }), /./);
  });

  it('changed file unknown to the graph still yields a step for it', () => {
    // ASSUMPTION: unknown files rebuild themselves rather than being
    // silently skipped (fail-closed against poisoned-cache skips).
    const { planRebuild } = loadImpl();
    const result = planRebuild({
      changedFiles: ['new-file.cc'],
      depGraph: smallGraph(),
      cacheHits: {},
    });
    assert.ok(result.steps.includes('new-file.cc'));
  });
});
