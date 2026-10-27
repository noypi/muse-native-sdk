'use strict';
// T7 — watch mode: affected module graph only, hot-reload preserving
// (NFR-BUILD-013 / AC-BUILD-043). Hermetic: stdlib only. Pure graph-diff —
// no fs watcher (fs wiring is dx-owned per the plan), no filesystem,
// no network, no secrets, no live services.
// graph[m] lists m's direct dependencies (modules m imports); recompiled is
// each changed node plus its transitive dependents via reverse reachability.

function recompileChanged(graph, changed) {
  const nodes = graph !== null && typeof graph === 'object' ? graph : {};
  const seeds = Array.isArray(changed) ? changed : [];

  // Reverse edges: dependency -> direct dependents. Own arrays only; never
  // mutates the input graph.
  const dependents = new Map();
  for (const mod of Object.keys(nodes)) {
    const deps = nodes[mod];
    if (!Array.isArray(deps)) {
      continue;
    }
    for (const dep of deps) {
      if (typeof dep !== 'string') {
        continue;
      }
      let list = dependents.get(dep);
      if (list === undefined) {
        list = [];
        dependents.set(dep, list);
      }
      list.push(mod);
    }
  }

  const seen = new Set();
  const queue = [];
  for (const seed of seeds) {
    if (typeof seed !== 'string') {
      continue;
    }
    if (!seen.has(seed)) {
      seen.add(seed);
      queue.push(seed);
    }
  }

  while (queue.length > 0) {
    const current = queue.shift();
    const nexts = dependents.get(current);
    if (nexts === undefined) {
      continue;
    }
    for (const next of nexts) {
      if (!seen.has(next)) {
        seen.add(next);
        queue.push(next);
      }
    }
  }

  return { recompiled: [...seen], preservedState: true };
}

module.exports = { recompileChanged };
