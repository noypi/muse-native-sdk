'use strict';

function reportCache(input) {
  const args = input !== null && typeof input === 'object' && !Array.isArray(input) ? input : {};
  const { hits, misses, floor } = args;
  if (
    typeof hits !== 'number' ||
    typeof misses !== 'number' ||
    !Number.isFinite(hits) ||
    !Number.isFinite(misses) ||
    hits < 0 ||
    misses < 0
  ) {
    throw new Error('cache-report: non-negative counts required');
  }
  const total = hits + misses;
  const hitRate = total === 0 ? 1 : hits / total;
  const ok = hitRate >= floor;
  const warning = ok ? null : `cache-report: hitRate ${hitRate} below floor ${floor}`;
  return { hitRate, ok, warning };
}

module.exports = { reportCache };
