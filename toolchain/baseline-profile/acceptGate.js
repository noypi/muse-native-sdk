'use strict';

// T6 — Baseline accept gate: non-empty profile + measured improvement + fresh
// (NFR-BUILD-006 / AC-BUILD-010).
// Composes T4 (freshness) + T5 (benchmark delta) plus non-empty rules,
// re-expressed over input values with no cross-module imports. ok:true only
// when all three legs pass; reasons name every failing leg.

function isValidSamples(samples) {
  if (!Array.isArray(samples) || samples.length === 0) {
    return false;
  }
  return samples.every(
    (value) => typeof value === 'number' && Number.isFinite(value) && value > 0,
  );
}

function medianOf(values) {
  const sorted = [...values].sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  if (sorted.length % 2 === 1) {
    return sorted[mid];
  }
  return (sorted[mid - 1] + sorted[mid]) / 2;
}

function acceptRelease(profile, sources, baselineMs, candidateMs) {
  const entry = profile !== null && typeof profile === 'object' ? profile : {};
  const origin = sources !== null && typeof sources === 'object' ? sources : {};
  const reasons = [];

  if (typeof entry.rules !== 'string' || entry.rules.trim().length === 0) {
    reasons.push('missing or empty profile rules');
  }

  if (
    typeof entry.sourceStamp !== 'number' ||
    !Number.isFinite(entry.sourceStamp) ||
    typeof origin.stamp !== 'number' ||
    !Number.isFinite(origin.stamp)
  ) {
    reasons.push(
      `stale profile: invalid stamp (sourceStamp ${String(entry.sourceStamp)}, sources stamp ${String(origin.stamp)}) treated as stale`,
    );
  } else if (entry.sourceStamp < origin.stamp) {
    reasons.push(
      `stale profile: sourceStamp ${entry.sourceStamp} older than sources stamp ${origin.stamp}`,
    );
  }

  if (!isValidSamples(baselineMs) || !isValidSamples(candidateMs)) {
    reasons.push('no startup improvement: invalid or empty benchmark samples');
  } else {
    const baselineMedian = medianOf(baselineMs);
    const candidateMedian = medianOf(candidateMs);
    const deltaMs = candidateMedian - baselineMedian;
    if (!(deltaMs < 0)) {
      reasons.push(
        `startup regressed: candidate median ${candidateMedian} not lower than baseline median ${baselineMedian} (delta ${deltaMs} ms)`,
      );
    }
  }

  return { ok: reasons.length === 0, reasons };
}

module.exports = { acceptRelease };
