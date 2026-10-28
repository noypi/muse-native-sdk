'use strict';
// evaluateSwap — Meta-First swap contract (FR-MUSE-001, AC-MUSE-002).
// Pure, synchronous, no I/O. Compares a Muse fixture against a
// generic-provider fixture and reports per-surface breakage.

function isPolygonMask(mask) {
  if (!mask || mask.encoding !== 'polygon') return false;
  if (!Array.isArray(mask.points) || mask.points.length === 0) return false;
  return mask.points.every(
    (pt) =>
      Array.isArray(pt) &&
      pt.length === 2 &&
      pt.every((n) => typeof n === 'number' && Number.isFinite(n)),
  );
}

function checkPolygonViz(genericResult) {
  try {
    const segment = genericResult && genericResult.segment;
    const matches = segment && segment.matches;
    if (!Array.isArray(matches) || matches.length === 0) return 'broken';
    return isPolygonMask(matches[0] && matches[0].mask) ? 'ok' : 'broken';
  } catch (_) {
    return 'broken';
  }
}

function checkThinkingStream(genericResult) {
  try {
    const deltas = genericResult && genericResult.reasonDeltas;
    if (!Array.isArray(deltas) || deltas.length === 0) return 'broken';
    const thinkingCount = deltas.filter(
      (d) => d && typeof d.reasoning_content === 'string' && d.reasoning_content.length > 0,
    ).length;
    return thinkingCount > 0 ? 'ok' : 'broken';
  } catch (_) {
    return 'broken';
  }
}

function checkStructuredOutput(genericResult) {
  try {
    const structure = genericResult && genericResult.structure;
    if (!structure || typeof structure.summary !== 'string' || structure.summary.length === 0) {
      return 'degraded';
    }
    if (!Array.isArray(structure.groups) || structure.groups.length === 0) return 'degraded';
    for (const group of structure.groups) {
      if (!group || typeof group !== 'object') return 'degraded';
      if (!Array.isArray(group.items) || group.items.length === 0) return 'degraded';
    }
    return 'ok';
  } catch (_) {
    return 'degraded';
  }
}

// evaluateSwap(museResult, genericResult) — museResult is the reference;
// per-surface verdicts are read from the generic-provider fixture so the
// test is falsifiable (generic replaced by Muse fixture → all ok).
function evaluateSwap(museResult, genericResult) {
  void museResult;
  const polygonViz = checkPolygonViz(genericResult);
  const thinkingStream = checkThinkingStream(genericResult);
  const structuredOutput = checkStructuredOutput(genericResult);
  const overall =
    polygonViz === 'ok' && thinkingStream === 'ok' && structuredOutput === 'ok'
      ? 'meta-first'
      : 'generic-degraded';
  return { polygonViz, thinkingStream, structuredOutput, overall };
}

module.exports = { evaluateSwap };
