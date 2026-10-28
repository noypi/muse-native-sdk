'use strict';

const NOOP_HIT_RATE_FLOOR = 0.99;

function planRebuild(input) {
  if (input === undefined || input === null || typeof input !== 'object' || Array.isArray(input)) {
    throw new Error('incremental-cycle: input missing');
  }
  const { changedFiles, depGraph, cacheHits } = input;
  if (!Array.isArray(changedFiles)) {
    throw new Error('incremental-cycle: changedFiles missing');
  }
  if (depGraph === undefined || depGraph === null || typeof depGraph !== 'object' || Array.isArray(depGraph)) {
    throw new Error('incremental-cycle: depGraph missing');
  }
  const hits = cacheHits !== undefined && cacheHits !== null ? cacheHits : {};
  const nodes = Object.keys(depGraph);

  if (changedFiles.length === 0) {
    return { steps: [], cacheHitRate: 1 };
  }

  const stepSet = new Set();
  const queue = [...changedFiles];
  for (const file of changedFiles) {
    stepSet.add(file);
  }
  while (queue.length > 0) {
    const current = queue.shift();
    const dependents = depGraph[current];
    if (!Array.isArray(dependents)) {
      continue;
    }
    for (const dependent of dependents) {
      if (!stepSet.has(dependent)) {
        stepSet.add(dependent);
        queue.push(dependent);
      }
    }
  }

  const steps = [...stepSet];
  const total = nodes.length;
  let cacheHitRate = 0;
  if (total > 0) {
    let hitCount = 0;
    for (const node of nodes) {
      if (!stepSet.has(node) && hits[node] === true) {
        hitCount += 1;
      }
    }
    cacheHitRate = hitCount / total;
  }
  return { steps, cacheHitRate };
}

function verifyNoop(input) {
  if (input === undefined || input === null || typeof input !== 'object' || Array.isArray(input)) {
    return { ok: false, reasons: ['noop: input missing'] };
  }
  const { changedFiles, artifactHashBefore, artifactHashAfter, cacheHitRate } = input;
  const reasons = [];

  if (!Array.isArray(changedFiles)) {
    reasons.push('noop: changed files unknown');
  } else if (changedFiles.length > 0) {
    reasons.push(`noop: changed files present: ${changedFiles.join(', ')}`);
  }

  if (
    typeof artifactHashBefore !== 'string' ||
    artifactHashBefore.length === 0 ||
    typeof artifactHashAfter !== 'string' ||
    artifactHashAfter.length === 0
  ) {
    reasons.push('noop: artifact hash missing');
  } else if (artifactHashBefore !== artifactHashAfter) {
    reasons.push('noop: artifact hash changed');
  }

  if (typeof cacheHitRate !== 'number' || !Number.isFinite(cacheHitRate)) {
    reasons.push(`noop: cache hit rate invalid: ${String(cacheHitRate)}`);
  } else if (cacheHitRate < NOOP_HIT_RATE_FLOOR) {
    reasons.push(`noop: cache hit rate ${cacheHitRate} below floor ${NOOP_HIT_RATE_FLOOR}`);
  }

  return { ok: reasons.length === 0, reasons };
}

module.exports = { planRebuild, verifyNoop };
