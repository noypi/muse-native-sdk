'use strict';

// T5 — Macrobenchmark delta comparator proves startup improvement before accept
// (NFR-BUILD-006 / AC-BUILD-009).
// Hermetic comparator over recorded fixtures (no live device): ok:true only
// when the candidate median is strictly lower than the baseline median.
// Median is the middle of the sorted values; for even-length inputs it is the
// mean of the two middle values. Timings must be finite numbers > 0; anything
// else fails closed.

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

function compareStartup(baselineMs, candidateMs) {
  if (!isValidSamples(baselineMs)) {
    return { ok: false, deltaMs: 0, reason: 'invalid or empty baseline samples' };
  }
  if (!isValidSamples(candidateMs)) {
    return { ok: false, deltaMs: 0, reason: 'invalid or empty candidate samples' };
  }
  const baselineMedian = medianOf(baselineMs);
  const candidateMedian = medianOf(candidateMs);
  const deltaMs = candidateMedian - baselineMedian;
  if (deltaMs < 0) {
    return {
      ok: true,
      deltaMs,
      reason: `startup improved by ${-deltaMs} ms (median ${candidateMedian} < ${baselineMedian})`,
    };
  }
  return {
    ok: false,
    deltaMs,
    reason: `no startup improvement (candidate median ${candidateMedian} not lower than baseline median ${baselineMedian})`,
  };
}

module.exports = { compareStartup };
