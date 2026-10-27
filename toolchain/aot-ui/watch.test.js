'use strict';
// T7 — watch mode: affected module graph only, hot-reload preserving
// (NFR-BUILD-013 / AC-BUILD-043). Hermetic: node:test + node:assert/strict +
// stdlib only. Pure graph-diff unit — no fs watcher (fs wiring is dx-owned
// per the plan), no filesystem, no network, no secrets, no live services.
// Red state: ./watch.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND) until T7 lands.
//
// Interface under test (plan verbatim):
//   recompileChanged(graph: Record<string,string[]>, changed: string[]): { recompiled: string[], preservedState: boolean } — `recompiled` = transitive dependents of `changed` only; `preservedState:true` signals hot-reload-compatible (no full rebuild).
//
// ASSUMPTION (edge direction): `graph[m]` lists m's direct dependencies
// (modules m imports). Dependents are computed via reverse reachability.
// ASSUMPTION (self-inclusion): `recompiled` includes each changed node plus
// its transitive dependents, each exactly once; unrelated modules excluded.
// ASSUMPTION (order): comparison is order-insensitive (tests sort); the
// coder owns result order.
// ASSUMPTION (unknown node): a changed node absent from the graph recompiles
// just itself with `preservedState:true`.
// ASSUMPTION (cycles): cyclic graphs terminate with each node listed once.
// ASSUMPTION (purity): inputs are never mutated.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./watch.js');
}

function sorted(names) {
  return [...names].sort();
}

// App -> Home -> shared; App -> Settings (unrelated to `shared` changes via Home only).
const GRAPH = {
  App: ['Home', 'Settings'],
  Home: ['shared'],
  Settings: [],
  shared: [],
};

describe('T7 — only the affected module graph recompiles (AC-BUILD-043)', () => {
  it('single-leaf change recompiles the leaf plus its transitive dependents only', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['shared']);
    assert.deepEqual(sorted(result.recompiled), ['App', 'Home', 'shared']);
  });

  it('excludes unrelated modules', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['shared']);
    assert.ok(!result.recompiled.includes('Settings'));
  });

  it('mid-graph change recompiles its dependents without touching others', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['Home']);
    assert.deepEqual(sorted(result.recompiled), ['App', 'Home']);
  });

  it('leaf change with no dependents recompiles just itself', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['App']);
    assert.deepEqual(result.recompiled, ['App']);
  });

  it('multiple changed nodes recompile the union, each once', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['Home', 'Settings']);
    assert.deepEqual(sorted(result.recompiled), ['App', 'Home', 'Settings']);
  });

  it('empty changed list recompiles nothing', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['shared']);
    assert.ok(result.recompiled.length > 0); // sanity: fixture is connected
    const empty = recompileChanged(GRAPH, []);
    assert.deepEqual(empty.recompiled, []);
    assert.equal(empty.preservedState, true);
  });

  it('diamond dependents are listed exactly once', () => {
    const { recompileChanged } = loadImpl();
    const diamond = { Top: ['L', 'R'], L: ['Base'], R: ['Base'], Base: [] };
    const result = recompileChanged(diamond, ['Base']);
    assert.deepEqual(sorted(result.recompiled), ['Base', 'L', 'R', 'Top']);
  });

  it('cyclic graphs terminate with each node listed once', () => {
    const { recompileChanged } = loadImpl();
    const cyclic = { A: ['B'], B: ['A', 'C'], C: [] };
    const result = recompileChanged(cyclic, ['C']);
    assert.deepEqual(sorted(result.recompiled), ['A', 'B', 'C']);
  });

  it('unknown changed node recompiles just itself', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['screens/New']);
    assert.deepEqual(result.recompiled, ['screens/New']);
    assert.equal(result.preservedState, true);
  });
});

describe('T7 — hot-reload preservation signal (AC-BUILD-043)', () => {
  it('preservedState:true on a single-leaf change (no full rebuild)', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['shared']);
    assert.equal(result.preservedState, true);
  });

  it('result contract shape is { recompiled: string[], preservedState: boolean }', () => {
    const { recompileChanged } = loadImpl();
    const result = recompileChanged(GRAPH, ['Home']);
    assert.ok(Array.isArray(result.recompiled));
    assert.ok(result.recompiled.every((m) => typeof m === 'string'));
    assert.equal(result.preservedState, true);
  });

  it('does not mutate the input graph or changed list', () => {
    const { recompileChanged } = loadImpl();
    const graph = structuredClone(GRAPH);
    const changed = ['shared'];
    recompileChanged(graph, changed);
    assert.deepEqual(graph, GRAPH);
    assert.deepEqual(changed, ['shared']);
  });
});
